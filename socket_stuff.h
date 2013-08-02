#ifndef SOCKET_STUFF_INCLUDED
#define SOCKET_STUFF_INCLUDED
#ifdef _WIN32
#include <winsock.h>
#else
#include <arpa/inet.h>
#include <netinet/ip.h>
#include <netdb.h>
#include <unistd.h>
typedef unsigned int ULONG;
typedef int SOCKET;
typedef sockaddr_in SOCKADDR_IN;
typedef sockaddr SOCKADDR;
typedef unsigned int DWORD;
typedef short unsigned int WORD;
#define INVALID_SOCKET -1
#define SOCKET_ERROR -1
#define closesocket close
#endif

#include <string>
#include <vector>
#include <iostream>
using namespace std;

class TSocketFrame
{
	TSocketFrame(TSocketFrame&);// ha ezt tolod, g�z van
	TSocketFrame& operator=(TSocketFrame&);//pointerekkel ilyent nem j�tszunk.
	void EnlargeBuffer(int mekkorara);
public:
	unsigned char* data; // cs�nj�n.
	int cursor;
	int datalen;

	// �tveszi a char* kezel�s�t, felszabad�tja destru�l�skor.
	TSocketFrame(unsigned char* data,int datalen):data(data),cursor(0),datalen(datalen){}
	TSocketFrame():data(0),cursor(0),datalen(0){}
	~TSocketFrame(){if (data) delete[] data;}

	void Reset(){cursor=0;}
	unsigned char ReadChar();
	unsigned int ReadWord();
	int ReadInt();
	string ReadString();
	void WriteChar(unsigned char mit);
	void WriteBytes(unsigned char* mit, int n);
	void WriteWord(unsigned int mit);
	void WriteInt(int mit);
	void WriteString(const string& mit);
};


class TBufferedSocket
{
protected:
	SOCKET sock;
	vector<char> recvbuffer;
	vector<char> sendbuffer;
	TBufferedSocket(const TBufferedSocket& mirol);
	TBufferedSocket& operator= (const TBufferedSocket& mirol);
	bool closeaftersend;
	int error;// ha nem 0, g�z volt.
public:
	
	// A f�ggv�ny megh�v�s�val elfogadja a szerz�d�si felt�teleket
	// miszerint teljesen lemond a socket haszn�lat�r�l.
	TBufferedSocket(SOCKET sock):sock(sock), closeaftersend(false),error(0){};
	TBufferedSocket(const string& hostname,int port);
	~TBufferedSocket(){ closesocket(sock); }

	int GetError(){return error;} //read only a v�ltoz�.
	void Update(); //H�vjad sokat. Mert alattam az a kett� csak a buffereket n�zi
	bool RecvFrame(TSocketFrame& hova);// L�gyszi �jonnan gener�ltat, mert �gyis fel�l�rja.
	void SendFrame(const TSocketFrame& mit,bool final=false);// L�gyszi ne legyen �res.

	bool RecvBytes(vector<unsigned char>& hova,int bytes);
	void SendBytes(const unsigned char* mit,int bytes,bool final=false);// L�gyszi ne legyen �res.

	bool RecvLine(string& hova); // \r\n n�lk�l
	const string RecvLine2(); //ha nem �rdekel hogy sikr�lt-e, hanem mindenk�pp kell egy string
	void SendLine(const string& mit, bool final=false);// \r\n n�lk�l

	
};

// abstract class virtual callback-ekkel
template <class TContext, class TSocket=TBufferedSocket>
class TBufferedServer{
protected:
	class TMySocket: public TSocket{
	public:
		TContext context; //Default konstruktor!!!
		ULONG address; 
		TMySocket(SOCKET sock, ULONG address): 
			TSocket(sock),address(address){}
	};

	SOCKET listenersock;
	vector<TMySocket*> socketek;
	void DeleteSocket(int index);//�s close
	virtual void OnConnect(TMySocket& sock)=0;
	virtual void OnUpdate(TMySocket& sock)=0;
	virtual void OnBeforeDelete(TMySocket& sock)=0;
public:
	TBufferedServer(int port);
	~TBufferedServer(){ while (!socketek.empty()) DeleteSocket(0); }
	
	void Update();
};

class TUDPSocket{
    SOCKET sock;
public:
    int error;
    TUDPSocket(int port);
    ~TUDPSocket() { closesocket(sock); };
    bool Recv(TSocketFrame& hova, DWORD& ip, WORD& port);
	//majd implement�lom ha sz�ks�g lesz r�.
    //void Send(const TSocketFrame& mit, DWORD ip:DWORD, WORD port); 
};


int SelectForRead(SOCKET sock);
int SelectForWrite(SOCKET sock);
int SelectForError(SOCKET sock);


//////////////////INLINE IMPLEMENTACIO//////////////

////////// TBufferedServer ///////////

template <class TContext,class TSocket>
 TBufferedServer<TContext,TSocket>::TBufferedServer(int port)
{
	#ifdef _WIN32
	WSADATA data;
	WSAStartup(MAKEWORD(2, 2), &data);
	#endif

	SOCKADDR_IN sockaddr;
	sockaddr.sin_family = AF_INET;
	sockaddr.sin_addr.s_addr = INADDR_ANY;
	//sockaddr.sin_addr.s_addr = inet_addr("127.0.0.1"); // IP to listen on
	sockaddr.sin_port = htons(port);

	listenersock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (!listenersock)
		return;

	int YES=1;
	if (setsockopt( listenersock, SOL_SOCKET, SO_REUSEADDR, (char*)&YES, sizeof(YES) ))
		return;
	
	if(bind(listenersock, (SOCKADDR *)&sockaddr, sizeof(SOCKADDR)))
		return;

	if (listen(listenersock,SOMAXCONN))
		return;
}

template <class TContext,class TSocket>
void  TBufferedServer<TContext,TSocket>::Update()
{
	// accept stuff
	while(SelectForRead(listenersock))
	{
		sockaddr_in addr;
		#ifdef _WIN32
		int addrlen=sizeof(addr);
		#else
		socklen_t addrlen=sizeof(addr);
		#endif
		SOCKET sock=accept(listenersock,(sockaddr*)&addr,&addrlen);
		if (sock==INVALID_SOCKET)
			break; // h�ha...
		#ifdef _WIN32
		TMySocket* ujclient=new TMySocket(sock,addr.sin_addr.S_un.S_addr);
		#else
		TMySocket* ujclient=new TMySocket(sock,addr.sin_addr.s_addr);
		#endif
		socketek.push_back(ujclient);
		OnConnect(*socketek[socketek.size()-1]);
	}

	// update �s erroros cuccok kisz�r�sa
	int i=0;
	while((unsigned int)i<socketek.size())
	{
		socketek[i]->Update();
		if (socketek[i]->GetError())
			DeleteSocket(i);
		else
			++i;
	}

	// recv stuff
	int n=socketek.size();
	for (int i=0;i<n;++i)
		OnUpdate(*socketek[i]);
}

template <class TContext,class TSocket>
void TBufferedServer<TContext,TSocket>::DeleteSocket(int i)
{
	OnBeforeDelete(*socketek[i]);
	delete socketek[i]; // ez kl�zol is.
	socketek.erase(socketek.begin()+i);
}


#endif
