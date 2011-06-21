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


struct TConfig{
	int clientversion;				//kliens verzi�, ez alatt kickel
	unsigned char sharedkey[20];	//a killenk�nti kriptogr�fiai al��r�s kulcsa
	string webinterface;			//a webes adatb�zis hostneve
	string webinterfacedown;		//a webes adatb�zis let�lt� f�jlj�nak URL-je
	string webinterfaceup;			//aa webes adatb�zis killfelt�lt� f�jlj�nak URL-je
	string allowedchars;
	TConfig(const string& honnan)
	{
		ifstream fil(honnan.c_str());
		fil>>clientversion;
		for(int i=0;i<20;++i)
		{
			int tmp;
			fil>>hex>>tmp;
			sharedkey[i]=tmp;
		}
		fil>>webinterface;
		fil>>webinterfacedown;
		fil>>webinterfaceup;
		fil>>allowedchars;
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

	/* �llapot */
	int glyph;
	unsigned int lastrecv;
	unsigned int lastsend;
	bool loggedin;
	int x,y;
	unsigned char crypto[20];
	unsigned int floodtime;
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


class StickmanServer: public TBufferedServer<TStickContext>{
protected:

	map<string,TStickRecord> db;
	map<string,int> killdb;

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
			sock.context.crypto[i]=rand();
		sock.context.lastrecv=GetTickCount();
		sock.context.lastsend=0;
		sock.context.kills=0;
		sock.context.ip=sock.address;
		sock.context.floodtime=0;
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
		frame.WriteInt(n);
		/*!TODO legk�zelebbi 50 kiv�laszt�sa */
		for(int i=0;i<n;++i)
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
		int ip=sock.context.ip;
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
		frame.WriteChar(mire);
		sock.SendFrame(frame);
	}

	void OnMsgLogin(TMySocket& sock,TSocketFrame& msg)
	{
		int verzio=msg.ReadInt();
		if (verzio<config.clientversion )
		{
			SendKick(sock,"Kerlek update-eld a jatekot a  http://stickman.hu oldalon",true);
			return;
		}
		if (sock.context.loggedin)
		{
			SendKick(sock,"Protocol error: already logged in",true);
			return;
		}
		string& nev=sock.context.nev=msg.ReadString();
		int n=nev.length();
		if (n==0)
		{
			SendKick(sock,"Legy szives adj meg egy nevet.",true);
			return;
		}
		if (n>15)
		{
			SendKick(sock,"A nev tul hosszu. Ami fura mert a kliens ezt alapbol nem engedi...",true);
			return;
		}
		for(int i=0;i<n;++i)
			if (config.allowedchars.find(nev[i])==string::npos)
			{
				SendKick(sock,"A nev meg nem engedett karaktereket tartalmaz.",true);
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
			SendKick(sock,"Protocol error: login",true);
			return;
		}
		int ip=sock.context.ip;
		cout<<"Login "<<sock.context.nev<<" from "<<((ip)&0xff)<<"."<<((ip>>8)&0xff)<<"."<<((ip>>16)&0xff)<<"."<<((ip>>24)&0xff)<<endl;
		
		if (db.count(nev))//regisztr�lt player
		{
			TStickRecord& record=db[nev];
			if (record.jelszo!=jelszo)
			{
				if (record.jelszo=="regi")
					SendKick(sock,"Kerlek ujitsd meg a regisztraciod a http://stickman.hu/ oldalon",true);
				else
					SendKick(sock,"Hibas jelszo.",false);
				return;
			}
			sock.context.clan=record.clan;
			sock.context.registered=true;
			SendLoginOk(sock);
			string chatuzi="\x11\x01Udvozollek ujra a jatekban, \x11\x03"+nev+"\x11\x01.";
			if(record.level)
				chatuzi=chatuzi+" A weboldalon erkezett "+itoa(record.level)+" leveled.";
			SendChat(sock,chatuzi);
			SendWeather(sock,weathermost);
		}
		else
		if (jelszo.length()==0)
		{
			SendLoginOk(sock);
			string chatuzi="\x11\x01Udvozollek a jatekban, \x11\x03"+nev+"\x11\x01. Ha tetszik a jatek, erdemes regisztralni a stickman.hu oldalon.";
			SendChat(sock,chatuzi);
			SendWeather(sock,weathermost);
		}
		else
		{
			SendKick(sock,"Ez a nev nincs regisztralva, igy jelszo nelkul lehet csak hasznalni.",false);
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
			SendKick(sock,"Protocol error: status",true);
			return;
		}
	}

	void ChatCommand(TMySocket& sock,const string& command,const string& parameter)
	{
		/* user commandok */
		
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
		if (command=="kick")
		{
			int kickuid=atoi(parameter.c_str());
			int pos=parameter.find(' ');
			string uzenet;
			if(pos>=0)
				uzenet.assign(parameter.begin()+pos,parameter.end());
			else
				uzenet="Legkozelebb ne legy balfasz.";
			int n=socketek.size();
			for(int i=0;i<n;++i)
				if (socketek[i]->context.UID==kickuid)
				{
					SendKick(*socketek[i],"Admin kickelt: "+uzenet);
					SendChatToAll("\x11\xe0" "Admin kickelte \x11\x03"+socketek[i]->context.nev+"\x11\xe0-t: "+uzenet);
					break;
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
	}

	void OnMsgChat(TMySocket& sock,TSocketFrame& msg)
	{
		string uzenet=msg.ReadString();
		if (msg.cursor!=msg.datalen) //nem jo a packetmeret
		{
			SendKick(sock,"Protocol error: chat",true);
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
			SendKick(sock,"Ne irj ennyi uzenetet egymas utan.",true);
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
			if (sock.context.nev=="Admin")
				uzenet="\x11\xe0"+sock.context.nev+"\x11\x03: "+uzenet;
			else
				uzenet="\x11\x01"+sock.context.nev+"\x11\x03: "+uzenet;
			if (sock.context.clan.length()>0)
				uzenet="\x11\x10"+sock.context.clan+" "+uzenet;

			SendChatToAll(uzenet,sock.context.glyph);
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
				SendKick(sock,"Protocol error: kill verification",true);
				return;
			}
		

		if (msg.cursor!=msg.datalen) //nem jo a packetmeret
		{
			SendKick(sock,"Protocol error: kill",true);
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
					if (killdb.count(nev))
						killdb[nev]+=1;
					else
						killdb[nev]=1;
					db[nev].napikill+=1;
					db[nev].osszkill+=1;
					SendChat(*socketek[i],"\x11\x01Megolted \x11\x03"+sock.context.nev+"\x11\x01-t.");
					SendChat(sock,"\x11\x03"+nev+" \x11\x01megolt teged.");
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
			while(!sock.error)
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
			while(!sock.error)
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
				SendKick(sock,"Protocol error: not logged in",true);
			else
			switch(type)
			{
				case CLIENTMSG_LOGIN:	OnMsgLogin(sock,recvd); break;
				case CLIENTMSG_STATUS:	OnMsgStatus(sock,recvd); break;
				case CLIENTMSG_CHAT:	OnMsgChat(sock,recvd); break;
				case CLIENTMSG_KILLED:	OnMsgKill(sock,recvd); break;
				default:
					SendKick(sock,"Protocol error: unknown packet type",true);
			}
			sock.context.lastrecv=GetTickCount();
		}

		/* 2000-2500 msenk�nt k�ld�nk playerlist�t */
		if (sock.context.loggedin &&
			sock.context.lastsend<GetTickCount()-2000-(rand()&511))
		{
			SendPlayerList(sock);
			sock.context.lastsend=GetTickCount();
		}
		/* 10 m�sodperc t�tlens�g ut�n kick van. */
		if (sock.context.lastrecv<GetTickCount()-10000)
			SendKick(sock,"Ping timeout",false);
	}
public:

	StickmanServer(int port): TBufferedServer<TStickContext>(port),
		lastUID(1),lastUDB(0),lastUDBsuccess(0),lastweather(0),weathermost(8),weathercel(15){}

	void Update()
	{
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
				SendWeather(*socketek[i],weathermost);
			lastweather=GetTickCount();
		}
	}
};


int main(){
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
