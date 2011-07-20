#define _CRT_SECURE_NO_WARNINGS
#include "socket_stuff.h"
#include "crypt_stuff.h"
#include "standard_stuff.h"
#include <string>
#include <set>
#include <map>
#include <iostream>
#include <fstream>
#include <stdlib.h>
#include <time.h>
#ifdef _WIN32
#include <windows.h>

#else
//mi a fasznak van egyatalan SIGPIPE?! Az eszem megall.
#include <signal.h>

#include <sys/time.h>

int GetTickCount()
{
	timespec tim;
	clock_gettime(CLOCK_MONOTONIC,&tim);
	return tim.tv_sec*1000+tim.tv_nsec/1000000;
}

void Sleep(int msec)
{
	usleep(msec*1000);
}
typedef unsigned short WORD;


#endif


using namespace std;


const struct TConfig{
	int clientversion;				//kliens verzi�, ez alatt kickel
	unsigned char sharedkey[20];	//a killenk�nti kriptogr�fiai al��r�s kulcsa
	string webinterface;			//a webes adatb�zis hostneve
	string webinterfacedown;		//a webes adatb�zis let�lt� f�jlj�nak URL-je
	string webinterfaceup;			//aa webes adatb�zis killfelt�lt� f�jlj�nak URL-je
	string allowedchars;			//n�vben megengedett karakterek
	vector<string> csunyaszavak;	//cenz�r�zand� szavak
	vector<string> viragnevek;		//amivel lecser�lj�k a cenz�r�zand� szavakat.
	TConfig(const string& honnan)
	{
		ifstream fil(honnan.c_str());
		fil>>clientversion;
		for(int i=0;i<20;++i)
		{
			int tmp;
			fil>>hex>>tmp;
			sharedkey[i]=(unsigned char)tmp;
		}
		fil>>webinterface;
		fil>>webinterfacedown;
		fil>>webinterfaceup;
		fil>>allowedchars;

		string tmpstr;

		fil>>tmpstr; 
		csunyaszavak=explode(tmpstr,",");

		fil>>tmpstr; 
		viragnevek=explode(tmpstr,",");
	}

} config("config.cfg");

struct TStickContext{
	/* adatok */
	string nev;
	string clan;
	bool registered;
	int fegyver;
	int fejrevalo;
	int UID;
	unsigned long ip;
	unsigned short port;
	int kills; //mai napon
	int checksum;
	int nyelv;

	/* �llapot */
	int glyph;
	unsigned int lastrecv; //utols� fogadott csomag
	unsigned int lastsend; //utols� k�ld�tt playerlista
	unsigned int lastudpquery; //utols� k�r�s hogy kliens k�ldj�n UDP-t
	bool gotudp; //kaptunk UDP csomagot a port jav�t�shoz.
	int udpauth;
	bool loggedin;
	int x,y;
	unsigned char crypto[20];
	unsigned int floodtime;
	string realm;
	int lastwhisp;
};

struct TStickRecord{
	int id;
	string jelszo;
	int osszkill;
	int napikill;
	string clan;
	set<WORD> medal;
	int level;
};

#define CLIENTMSG_LOGIN 1
/*	Login �zenet. Erre v�lasz: LOGINOK, vagy KICK
	int kliens_verzi�
	int nyelv
	string n�v
	string jelsz�
	int fegyver
	int fejreval�
	char[2] port
	int checksum
*/

#define CLIENTMSG_STATUS 2
/*	Ennek az �zenetnek sok �rtelme nincs csak a kapcsolatot tartja fenn.
	int x
	int y
*/

#define CLIENTMSG_CHAT 3
/*	Chat, ennyi.
	string uzenet
*/

#define CLIENTMSG_KILLED 4
/*	Ha meg�lte a klienst valaki, ezt k�ldi.
	int UID
	char [20] crypto
*/

#define SERVERMSG_LOGINOK 1
/*
	int UID
	char [20] crypto
*/

#define SERVERMSG_PLAYERLIST 2
/*
	int num
	num*{
		char[4] ip
		char[2] port
		int uid
		string nev
		int fegyver
		int fejrevalo
		int killek
		}
*/

#define SERVERMSG_KICK 3
/*
	char hardkick (bool igaz�b�l)
	string indok
*/

#define SERVERMSG_CHAT 4
/*
	string uzenet
	int glyph
*/

#define SERVERMSG_WEATHER 5
/*
	byte mire
*/

#define SERVERMSG_SENDUDP 6
/*
  int auth
*/

class StickmanServer: public TBufferedServer<TStickContext>{
protected:
	const TSimpleLang lang;
	TUDPSocket udp;

	map<string,TStickRecord> db;
	map<string,int> killdb;

	map<string,string> bans;

	unsigned int lastUID;
	unsigned int lastUDB;
	unsigned int lastweather;
	time_t lastUDBsuccess;

	int weathermost;
	int weathercel;
	virtual void OnConnect(TMySocket& sock)
	{
		sock.context.loggedin=false;
 		sock.context.UID=++lastUID;
		for(int i=0;i<20;++i)
			sock.context.crypto[i]=(unsigned char)rand();
		sock.context.lastrecv=GetTickCount();
		sock.context.lastsend=0;
		sock.context.lastudpquery=0; //utols� k�r�s hogy kliens k�ldj�n UDP-t
		sock.context.gotudp=false; //ennyit v�runk a k�vetkez� k�r�sig
		sock.context.kills=0;
		sock.context.ip=sock.address;
		sock.context.floodtime=0;
		sock.context.udpauth=rand();
		sock.context.nyelv=0;
	}
	
	void SendKick(TMySocket& sock,const string& indok="Kicked without reason",bool hard=false)
	{
		TSocketFrame frame;
		frame.WriteChar(SERVERMSG_KICK);
		frame.WriteChar(hard?1:0);
		frame.WriteString(indok);
		sock.SendFrame(frame,true);
		cout<<"Kicked "<<sock.context.nev<<": "<<indok<<endl;
	}

	void SendPlayerList(TMySocket& sock)
	{
		TSocketFrame frame;
		frame.WriteChar(SERVERMSG_PLAYERLIST);
		int n=socketek.size();
		int n2=0;
		for(int i=0;i<n;++i)
			if(sock.context.realm==socketek[i]->context.realm)
				++n2;
		frame.WriteInt(n2);
		/*!TODO legk�zelebbi 50 kiv�laszt�sa */
		for(int i=0;i<n;++i)
		if(sock.context.realm==socketek[i]->context.realm)
		{
			TStickContext& scontext=socketek[i]->context;
			frame.WriteChar((unsigned char)(scontext.ip));
			frame.WriteChar((unsigned char)(scontext.ip>>8));
			frame.WriteChar((unsigned char)(scontext.ip>>16));
			frame.WriteChar((unsigned char)(scontext.ip>>24));

			frame.WriteChar((unsigned char)(scontext.port));
			frame.WriteChar((unsigned char)(scontext.port>>8));
		
			frame.WriteInt(scontext.UID);

			frame.WriteString(scontext.nev);

			frame.WriteInt(scontext.fegyver);
			frame.WriteInt(scontext.fejrevalo);
			frame.WriteInt(scontext.kills);
		}
		sock.SendFrame(frame);
	}

	void SendLoginOk(TMySocket& sock)
	{
		TSocketFrame frame;
		frame.WriteChar(SERVERMSG_LOGINOK);
		frame.WriteInt(sock.context.UID);
		for(int i=0;i<20;++i)
			frame.WriteChar(sock.context.crypto[i]);
		sock.SendFrame(frame);
	}

	void SendChat(TMySocket& sock,const string& uzenet,int showglyph=0)
	{
		TSocketFrame frame;
		frame.WriteChar(SERVERMSG_CHAT);
		frame.WriteString(uzenet);
		frame.WriteInt(showglyph);
		sock.SendFrame(frame);
	}

	void SendChatToAll(const string& uzenet,int showglyph=0)
	{
		int n=socketek.size();
		for(int i=0;i<n;++i)
			SendChat(*socketek[i],uzenet,showglyph);
	}

	void SendWeather(TMySocket& sock,int mire)
	{
		TSocketFrame frame;
		frame.WriteChar(SERVERMSG_WEATHER);
		frame.WriteChar((unsigned char)mire);
		sock.SendFrame(frame);
	}

	void SendUdpQuery(TMySocket& sock)
	{
		TSocketFrame frame;
		frame.WriteChar(SERVERMSG_SENDUDP);
		frame.WriteInt(sock.context.udpauth);
		sock.SendFrame(frame);
	}

	void OnMsgLogin(TMySocket& sock,TSocketFrame& msg)
	{
		int verzio=msg.ReadInt();
		int nyelv=sock.context.nyelv=msg.ReadInt();

		if (!lang.HasLang(nyelv))
			nyelv=0;

		if (verzio<config.clientversion )
		{
			SendKick(sock,lang(nyelv,1),true);
			return;
		}
		if (sock.context.loggedin)
		{
			SendKick(sock,lang(nyelv,2),true);
			return;
		}
		string& nev=sock.context.nev=msg.ReadString();
		int n=nev.length();
		if (n==0)
		{
			SendKick(sock,lang(nyelv,3),false);
			return;
		}
		if (n>15)
		{
			SendKick(sock,lang(nyelv,4),false);
			return;
		}
		for(int i=0;i<n;++i)
			if (config.allowedchars.find(nev[i])==string::npos)
			{
				SendKick(sock,lang(nyelv,5),false);
				return;
			}
		

		string jelszo=msg.ReadString();

		sock.context.fegyver=msg.ReadInt();
		sock.context.fejrevalo=msg.ReadInt();

		sock.context.port =msg.ReadChar();
		sock.context.port+=msg.ReadChar()<<8;
		sock.context.checksum=msg.ReadInt();
		if (msg.cursor!=msg.datalen) //nem jo a packetmeret
		{
			SendKick(sock,lang(nyelv,6),true);
			return;
		}
		int ip=sock.context.ip;
		cout<<"Login "<<sock.context.nev<<" from "<<((ip)&0xff)<<"."<<((ip>>8)&0xff)<<"."<<((ip>>16)&0xff)<<"."<<((ip>>24)&0xff)<<endl;
		
		if(bans.count(nev))
		{
				SendKick(sock,lang(nyelv,34)+bans[nev],false);
				return;
		}

		if(bans.count(itoa(ip)))
		{
				SendKick(sock,lang(nyelv,34)+bans[itoa(ip)],false);
				return;
		}

		if (db.count(nev))//regisztr�lt player
		{
			TStickRecord& record=db[nev];
			if (record.jelszo!=jelszo)
			{
				SendKick(sock,lang(nyelv,7),false);
				return;
			}
			sock.context.clan=record.clan;
			sock.context.registered=true;
			SendLoginOk(sock);
			string chatuzi="\x11\x01"+lang(nyelv,8)+"\x11\x03"+nev+"\x11\x01"+lang(nyelv,9);
			if(record.level)
				chatuzi=chatuzi+lang(nyelv,10)+itoa(record.level)+lang(nyelv,11);
			SendChat(sock,chatuzi);
			SendWeather(sock,weathermost);
		}
		else
		if (jelszo.length()==0)
		{
			SendLoginOk(sock);
			string chatuzi="\x11\x01"+lang(nyelv,12)+"\x11\x03"+nev+"\x11\x01"+lang(nyelv,13);
			SendChat(sock,chatuzi);
			SendWeather(sock,weathermost);
		}
		else
		{
			SendKick(sock,lang(nyelv,14),false);
			return;
		}

		sock.context.glyph=0;
		n=nev.length();
		for(int i=0;i<n;++i)
		{
			sock.context.glyph*=982451653;
			sock.context.glyph+=nev[i];
		}

		sock.context.glyph*=756065179;
		sock.context.loggedin=true;
	}

	void OnMsgStatus(TMySocket& sock,TSocketFrame& msg)
	{
		sock.context.x=msg.ReadInt();
		sock.context.y=msg.ReadInt();

		if (msg.cursor!=msg.datalen) //nem jo a packetmeret
		{
			SendKick(sock,lang(sock.context.nyelv,15),true);
			return;
		}
	}

	void ChatCommand(TMySocket& sock,const string& command,const string& parameter)
	{
		/* user commandok */

		if (command=="realm" && parameter.size()>0)
		{
			sock.context.realm=parameter;
			SendChat(sock,lang(sock.context.nyelv,16)+parameter+lang(sock.context.nyelv,17));
		}else
		if (command=="norealm" || (command=="realm" && parameter.size()==0))
		{
			sock.context.realm="";
			SendChat(sock,lang(sock.context.nyelv,18));
		}else
		if (command=="w" || command=="whisper")
		{
			int pos=parameter.find(' ');
			if(pos>=0)
			{
				int n=socketek.size();
				string kinek(parameter.begin(),parameter.begin()+pos);
				string uzenet(parameter.begin()+pos+1,parameter.end());
				for(int i=0;i<n;++i)
				{
					string& nev=socketek[i]->context.nev;
					if (nev==kinek)
					{
						ChatCleanup(uzenet);
						SendChat(*socketek[i],"\x11\xe3[From] "+sock.context.nev        +": "+uzenet,socketek[i]->context.glyph);
						SendChat(sock        ,"\x11\xe3[To] "  +socketek[i]->context.nev+": "+uzenet,sock.context.glyph);
						socketek[i]->context.lastwhisp=sock.context.UID;
						break;
					}
				}
			}
		}else
		if(command=="r" || command=="reply")
		{
			int n=socketek.size();
			for(int i=0;i<n;++i)
				if (socketek[i]->context.UID==sock.context.lastwhisp)
				{
					string uzenet=parameter;
					ChatCleanup(uzenet);
					SendChat(*socketek[i],"\x11\xe3[From] "+sock.context.nev        +": "+uzenet,socketek[i]->context.glyph);
					SendChat(sock        ,"\x11\xe3[To] "  +socketek[i]->context.nev+": "+uzenet,sock.context.glyph);
					socketek[i]->context.lastwhisp=sock.context.UID;
					break;
				}
		}else
		if((command=="c" || command=="clan") && sock.context.clan.size()>0)
		{
			string uzenet=parameter;
			ChatCleanup(uzenet);
			uzenet="\x11\x10"+sock.context.clan+" "+sock.context.nev+": "+uzenet;
			int n=socketek.size();
			for(int i=0;i<n;++i)
				if (socketek[i]->context.clan==sock.context.clan)
					SendChat(*socketek[i],uzenet,sock.context.glyph);
		}

		/* admin commandok */
		if (sock.context.nev!="Admin")
			return;

		if (command=="weather")
		{
			weathermost=weathercel=atoi(parameter.c_str());
			int n=socketek.size();
			for(int i=0;i<n;++i)
				SendWeather(*socketek[i],weathermost);
		}else	
		if (command=="kick" || command=="ban")
		{
			int n=socketek.size();
			bool kick=command=="kick";
			int kickuid=atoi(parameter.c_str());
			if (!kickuid)
				for(int i=0;i<n;++i)
					if (socketek[i]->context.nev==parameter)
						kickuid=socketek[i]->context.UID;

			int pos=parameter.find(' ');
			string uzenet;
			if(pos>=0)
				uzenet.assign(parameter.begin()+pos,parameter.end());
			else
				uzenet=lang(sock.context.nyelv,19); //lol lang specifikus default kick
			
			for(int i=0;i<n;++i)
				if (socketek[i]->context.UID==kickuid)
				{
					SendKick(*socketek[i],lang(sock.context.nyelv,kick?20:35)+uzenet,true);
					SendChatToAll("\x11\xe0"+lang(sock.context.nyelv,kick?21:36)+"\x11\x03"+
								  socketek[i]->context.nev+"\x11\xe0"+lang(sock.context.nyelv,22)+uzenet);
					if (!kick) 
						bans[itoa(socketek[i]->address)]=uzenet;
					break;
				}
		}else
		if (command=="banlist" )
		{
			for(map<string,string>::iterator i=bans.begin();i!=bans.end();++i)
				SendChat(sock,i->first+ ": "+i->second);
		}else
		if (command=="unban" )
		{
			if (bans.count(parameter))
			{
				SendChat(sock,"Unbanned "+parameter);
				bans.erase(parameter);
			}
		}else
		if (command=="uid")
		{
			int n=socketek.size();
			for(int i=0;i<n;++i)
			{
				string& nev=socketek[i]->context.nev;
				if (nev.find(parameter)!=string::npos)
					SendChat(sock,itoa(socketek[i]->context.UID)+" : "+nev);
			}
		}
		else
		if (command=="ip")
		{
			int n=socketek.size();
			for(int i=0;i<n;++i)
			{
				string& nev=socketek[i]->context.nev;
				if (nev.find(parameter)!=string::npos)
					SendChat(sock,string(inet_ntoa(*(in_addr*)&socketek[i]->address))+" : "+nev);
			}

		}
	}

	void ChatCleanup(string& uzenet)
	{
		int n=uzenet.length();
		for(int i=0;i<n;++i)
			if (uzenet[i]>=0 && uzenet[i]<0x20)
				uzenet[i]=0x20;

		int cn=config.csunyaszavak.size();
		int vn=config.viragnevek.size();
		string luzenet=uzenet;
		tolower(luzenet);
		for (int i=0;i<cn;++i)
		{
			string::size_type pos=0;
			while ( (pos = luzenet.find(config.csunyaszavak[i], pos)) != string::npos ) {
				uzenet.replace( pos, config.csunyaszavak[i].size(), config.viragnevek[rand()%vn] );
				pos++;
			}
		}

		char elobb=0;
		int cnt=0;
		unsigned int i=0;
		while(i<uzenet.size())
		{
			if (uzenet[i]==elobb)
				++cnt;
			else
			{
				elobb=uzenet[i];
				cnt=0;
			}
			if (cnt>3)
				uzenet.erase(i,1);
			else
				++i;
		}
	}

	void OnMsgChat(TMySocket& sock,TSocketFrame& msg)
	{
		string uzenet=msg.ReadString();
		if (msg.cursor!=msg.datalen) //nem jo a packetmeret
		{
			SendKick(sock,lang(sock.context.nyelv,23),true);
			return;
		}

		if (uzenet.length()<=1)
			return;

		//flood ellenorzes
		if (sock.context.floodtime<GetTickCount()-12000)
			sock.context.floodtime=GetTickCount()-10000;
		else
			sock.context.floodtime+=2000;
		if (sock.context.floodtime>GetTickCount())
		{
			SendKick(sock,lang(sock.context.nyelv,24),true);
			return;
		}

		if (uzenet[0]=='/')
		{
			int pos=uzenet.find(' ');
			if(pos>=0)
				ChatCommand(sock, 
				            string(uzenet.begin()+1,uzenet.begin()+pos),
							string(uzenet.begin()+pos+1,uzenet.end()));
			else
				ChatCommand(sock, 
				            string(uzenet.begin()+1,uzenet.end()),
							"");
		}
		else
		{
			ChatCleanup(uzenet);
			if (sock.context.nev=="Admin")
				uzenet="\x11\xe0"+sock.context.nev+"\x11\x03: "+uzenet;
			else
				uzenet="\x11\x01"+sock.context.nev+"\x11\x03: "+uzenet;
			if (sock.context.clan.length()>0)
				uzenet="\x11\x10"+sock.context.clan+" "+uzenet;

			int n=socketek.size();
			for(int i=0;i<n;++i)
				if (socketek[i]->context.realm==sock.context.realm)
					SendChat(*socketek[i],uzenet,sock.context.glyph);
		}
	}

	void OnMsgKill(TMySocket& sock,TSocketFrame& msg)
	{
		int UID=msg.ReadInt();

		// Minden kill ut�n seedet xorolunk a titkos kulccsal, �s egyet
		// r�hashel�nk a jelenlegi crypt �rt�kre.
		// Ez kell�en fos verifik�l�sa a killnek, de ennyivel kell be�rni
		// Thx Kirknek a security auditing�rt

		unsigned char newcrypto[20];
		for(int i=0;i<20;++i)
			newcrypto[i]=sock.context.crypto[i]^config.sharedkey[i];

		SHA1_Hash(newcrypto,20,sock.context.crypto);
		
		for(int i=0;i<20;++i)
			if (sock.context.crypto[i]!=msg.ReadChar())
			{
				SendKick(sock,lang(sock.context.nyelv,25),true);
				return;
			}
		

		if (msg.cursor!=msg.datalen) //nem jo a packetmeret
		{
			SendKick(sock,lang(sock.context.nyelv,26),true);
			return;
		}

		int n=socketek.size();
		for(int i=0;i<n;++i)
			if (socketek[i]->context.UID==UID &&
				socketek[i]->context.loggedin)
			{
				socketek[i]->context.kills+=1;
				if(socketek[i]->context.registered)
				{
					const string& nev=socketek[i]->context.nev;
					if (socketek[i]->context.realm.size()==0)
					{
						if (killdb.count(nev))
							killdb[nev]+=1;
						else
							killdb[nev]=1;
						db[nev].napikill+=1;
						db[nev].osszkill+=1;
					}
					SendChat(*socketek[i],"\x11\x01"+lang(socketek[i]->context.nyelv,27)+"\x11\x03"+sock.context.nev+"\x11\x01"+lang(socketek[i]->context.nyelv,28));
					SendChat(sock,"\x11\x01"+lang(sock.context.nyelv,29)+"\x11\x03"+nev+" \x11\x01"+lang(sock.context.nyelv,30));
					break;
				}
			}
		
	}


	void UpdateDb()
	{
		cout<<"Updating database..."<<endl;
		//post kills
		if(killdb.size()>0)
		{
			cout<<"Uploading kills "<<killdb.size()<<" kills."<<endl;
			TBufferedSocket sock("stickman.hu",80);

			string postmsg;
			postmsg.reserve(64*1024);
			for(map<string,int>::iterator i=killdb.begin();i!=killdb.end();++i)
			{
				const string& nev=i->first;
				postmsg+=nev+"\r\n";
				postmsg+=itoa(i->second)+"\r\n";
				for (set<WORD>::iterator j=db[nev].medal.begin();j!=db[nev].medal.end();++j)
				{
					postmsg+=((char)*j);
					postmsg+=((char)(*j>>8));
				}
				postmsg+="\r\n";
			}

			sock.SendLine("POST "+config.webinterfaceup+" HTTP/1.1");
			sock.SendLine("Host: "+config.webinterface);
			sock.SendLine("Connection: close");
			sock.SendLine("Content-Type: application/x-www-form-urlencoded");
			sock.SendLine("Content-Length: "+itoa(postmsg.length()+2));
			sock.SendLine("");
			sock.SendLine(postmsg);
			
			string lin;
			while(!sock.GetError())
			{
					sock.Update();
					Sleep(1);
			}
			//!TODO
			while(sock.RecvLine(lin))
				cout<<lin<<endl;
			/* while(sock.RecvLine(lin))
				delete killdb[lin] */
			killdb.clear();
		}

		//get db
		{
			TBufferedSocket sock(config.webinterface,80);

			char request[1024];
			sprintf(request,config.webinterfacedown.c_str(),lastUDBsuccess);			
			cout<<"Download req: "<<config.webinterface<<string(request)<<endl;
			sock.SendLine("GET "+string(request)+" HTTP/1.0");
			sock.SendLine("Host: "+config.webinterface);
			sock.SendLine("Connection: close");
			sock.SendLine("");

			//K�ldj�nk el �s recv-elj�nk mindent (kapcsolatz�r�sig)
			while(!sock.GetError())
			{
					sock.Update();
					Sleep(1);
			}

			//HTTP header �tugr�sa
			string headerline;
			do
			{
				if(!sock.RecvLine(headerline)) //v�ge szakad a cuccnak, ez nem �res sor!
				{
					cout<<"Problem: nincs http body."<<endl;
					headerline="shit.";
					break;
				}
			}while(headerline.length()>0);

			//Feldolgoz�s
			string lastname;
			while(1)
			{
				string nev;
				if(!sock.RecvLine(nev)) //v�ge szakad a cuccnak, ez nem �res sor!
				{
					cout<<"Problem: nem ures sorral er veget az adas."<<endl;				
					break;
				}
				if (nev.length()==0) //�res sor, ez j�, siker�lt �pd�telni
				{
					lastUDBsuccess=time(0);
					
					break;
				}
				string medal=sock.RecvLine2();

				TStickRecord ujrec;
				ujrec.id=atoi(sock.RecvLine2().c_str());
				ujrec.jelszo=sock.RecvLine2();
				ujrec.osszkill=atoi(sock.RecvLine2().c_str());
				ujrec.napikill=atoi(sock.RecvLine2().c_str());
				ujrec.clan=sock.RecvLine2();
				ujrec.level=atoi(sock.RecvLine2().c_str());
				int n=medal.length();
				for(int i=0;i<n;i+=2)
					ujrec.medal.insert(medal[i]|(medal[i+1]<<8));
				db[nev]=ujrec;
				lastname=nev;
			}
			cout<<"Last name: "<<lastname<<endl;
		}
		cout<<"Finished. "<<db.size()<<" player records."<<endl;
	}

	virtual void OnUpdate(TMySocket& sock)
	{
		TSocketFrame recvd;
		while(sock.RecvFrame(recvd))
		{
			char type=recvd.ReadChar();
			if (!sock.context.loggedin && type!=CLIENTMSG_LOGIN)
				SendKick(sock,lang(sock.context.nyelv,31),true);
			else
			switch(type)
			{
				case CLIENTMSG_LOGIN:	OnMsgLogin(sock,recvd); break;
				case CLIENTMSG_STATUS:	OnMsgStatus(sock,recvd); break;
				case CLIENTMSG_CHAT:	OnMsgChat(sock,recvd); break;
				case CLIENTMSG_KILLED:	OnMsgKill(sock,recvd); break;
				default:
					SendKick(sock,lang(sock.context.nyelv,32),true);
			}
			sock.context.lastrecv=GetTickCount();
		}

		unsigned int gtc=GetTickCount();
		/* 2000-2500 msenk�nt k�ld�nk playerlist�t */
		if (sock.context.loggedin &&
			sock.context.lastsend<gtc-2000-(rand()&511))
		{
			SendPlayerList(sock);
			sock.context.lastsend=gtc;
		}
		/* f�l m�sodpercenk�nt k�r�nk UDP-t ha nem j�tt m�g*/
		if (sock.context.loggedin &&
			sock.context.lastudpquery<gtc-500)
		{
			SendUdpQuery(sock);
			sock.context.lastudpquery=gtc;
		}
		/* 10 m�sodperc t�tlens�g ut�n kick van. */
		if (sock.context.lastrecv<gtc-10000)
			SendKick(sock,lang(sock.context.nyelv,33),false);

	}
public:

	StickmanServer(int port): TBufferedServer<TStickContext>(port),lang("lang.ini"),
		udp(port),lastUID(1),lastUDB(0),lastUDBsuccess(0),lastweather(0),weathermost(8),weathercel(15){}

	void Update()
	{
		TSocketFrame frame;
		DWORD fromip;
		WORD fromport;
		while(udp.Recv(frame,fromip,fromport))
		{
			int n=socketek.size();
			int stck=frame.ReadInt();
			if (stck!=('S'|('T'<<8)|('C'<<16)|('K'<<24)))
				continue;

			int uid=frame.ReadInt();
			int auth=frame.ReadInt();
			for(int i=0;i<n;++i)
				if (!socketek[i]->context.gotudp &&
					 socketek[i]->context.UID==uid && 
					 socketek[i]->context.udpauth==auth)
				{
					socketek[i]->context.port=fromport; //hopp, hazudott a portj�r�l tal�n.
					socketek[i]->context.gotudp=true;
				}
		}

		if (lastUDB<GetTickCount()-300000)//5 percenk�nt.
		{
			UpdateDb();
			lastUDB=GetTickCount();
		}

		TBufferedServer<TStickContext>::Update();

		if (lastweather<GetTickCount()-60000)//percenk�nt
		{
			if (weathermost==weathercel)
				weathercel=rand()%23;
			if(weathermost>weathercel)
				--weathermost;
			else
				++weathermost;

			int n=socketek.size();
			for(int i=0;i<n;++i)
				if (socketek[i]->context.loggedin)
					SendWeather(*socketek[i],weathermost);
			lastweather=GetTickCount();
		}
	}
};


int main(){
	#ifndef _WIN32
	signal(SIGPIPE, SIG_IGN);
	#endif
	cout<<"Stickserver starting..."<<endl;
	{
		StickmanServer server(25252);
		while(1)
		{
			server.Update();
			Sleep(10);
		}
	}
}
