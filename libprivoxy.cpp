#include "LibPrivoxy/stdafx.h"
#include "libprivoxy.h"
#include "LibPrivoxy/Utils.h"
#include "miscutil.h"
#include <assert.h>

/** @brief Many people want to integrate Privoxy into their own projects, 
* I made some changes to the Privoxy codes to compiled into a DLL or static library, 
* so that you can better integrate it into their own projects. https://www.sockscap64.com
*
* @Author: Taro, https://www.sockscap64.com
*/

#define DEFAULT_PRIVOXY_LISTEN_ADDR "127.0.0.1"
#define DEFAULT_PRIVOXY_LISTEN_PORT 25378
#define DEFAULT_SEARCH_AMOUNT 200

// ����������ο�
// http://www.privoxy.org/user-manual/config.html
// forward   /      parent-proxy.example.org:8080
// forward-socks4a   /              socks-gw.example.com:1080  .
#define FORWARD_PROXY "%s:%d%s%s"

#define SSCAP_PRIVOXY_CONFIG_TEMPLATE "listen-address %s:%d\n"	\
	"show-on-task-bar 0\n"	\
	"activity-animation 0\n"	\
	"%s / %s %s\n"	\
	"hide-console\n" \
	"accept-intercepted-requests 1\n" \
	"logdir %s\n" \
	"logfile privoxy-log.log\n"\
	"pac-file %s\n"

static bool g_privoxy_service_stareted = false;
static int g_port_of_privoxy_listen = 0;
extern "C" char g_szDirecotryOfLibPrivoxy[1024] = {0};
extern "C" HMODULE g_hLibPrivoxyModule = NULL;
extern "C" int g_terminate;

extern "C" void close_privoxy_listening_socket();

static bool init_libprivoxy_path()
{
	if( !g_hLibPrivoxyModule ) return false;

	if( !GetModuleFileName(g_hLibPrivoxyModule,g_szDirecotryOfLibPrivoxy, 1024 ) ) return false;

	TCHAR *p = _tcsrchr( g_szDirecotryOfLibPrivoxy , '\\' );

	if( p )
	{
		*p=0x00;
		return true;
	}

	return false;
}
static const char *get_forward_type( int proxy_type )
{
	if( proxy_type == HTTP )
		return "forward-http";
	else if( proxy_type == SOCKS4 )
		return "forward-socks4";
	else if( proxy_type == SOCKS5 )
		return "forward-socks5";

	return "forward-socks5";
}

static BOOL wirte_parameters_to_config_file( 
	int proxy_type, /* HTTP,SOCKS4,SOCKS5 */
	const char *forward_socks_5_ip,
	int forward_socks5_port,
	const char *base64_userpass,
	const char *listen_addr,
	int listen_port ,
	const char *pac_file
	)
{
	TCHAR strConfigFile[1024] = {0};
	const char *strForward = get_forward_type( proxy_type );
	_stprintf_s( strConfigFile,1024, _T("%s\\privoxy.conf"), g_szDirecotryOfLibPrivoxy );

	//char szBuffer[8000] = {0};
	char szForwardProxy[1000]= {0};
	char *EncryptProxy = NULL;
	char *ConfigFileBody = NULL;

	snprintf( szForwardProxy, 1000, FORWARD_PROXY,
		forward_socks_5_ip, forward_socks5_port,base64_userpass?"@":"",base64_userpass?base64_userpass:""
		);

#ifdef FEATURE_ENCRYPTCFG
	EncryptProxy = encrypt_msg( szForwardProxy,strlen( szForwardProxy ), TRUE );
#else
	EncryptProxy = strdup( szForwardProxy );
#endif
	if( !EncryptProxy ) return FALSE;

	ConfigFileBody = strdup_printf( SSCAP_PRIVOXY_CONFIG_TEMPLATE, 
		listen_addr, listen_port,
		strForward, EncryptProxy, ".",
		g_szDirecotryOfLibPrivoxy,
		pac_file
		);

	free( EncryptProxy );

	if( !ConfigFileBody )
	{
		return FALSE;
	}

	TCHAR *open_file_mode = _T("wb");
	FILE *f=NULL;
	f = _tfsopen( strConfigFile, open_file_mode, _SH_DENYNO );
	if( f )
	{
		fwrite( ConfigFileBody,1,strlen(ConfigFileBody),f );
		fflush( f );
		fclose( f );
		free( ConfigFileBody );

		return TRUE;
	}

	free( ConfigFileBody );

	return FALSE;
}

//////////////////////////////////////////////////////////////////////////
//
/** @brief ����privoxy, ���Է�������, ����������Ѿ�����, �ٴε���ֻ�����privoxy�����������Ϣ, 
* �������forwarding socks5 ip,forwarding port, privoxy service port����Ϣ
*
* @param listen_port: PRIVOXY�����˿�, 0�Զ������������ö˿�
* @param forward_socks_5_ip: ת������SOCKS 5 IP
* @param forward_socks5_port: ת������SOCKS 5 PORT
* @param pac_file: pac �ļ�����·��
*        �û���չPAC�ļ���һ���ı��ļ�, ���ָ����Ҫ�������������,ÿ��һ��, ���з��ָ�
*        ��:
*        domain1.com
*        domain2.com
*        domain3.com
*
* @return 0 �����ɹ�, ��������ʧ��
*	1 ��ʼ��libprivoxy·��ʧ��
*	1 �Ҳ������õĶ˿�����privoxy����
*	2 д�����ò���ʱ����
*/
extern "C" LIBPRIVOXY_API int __stdcall start_privoxy( 
	int proxy_type, /* HTTP,SOCKS4,SOCKS5 */
	const char *forward_socks_5_ip, 
	int forward_socks5_port,
	const char *username,
	const char *password,
	const char *listen_addr,
	int listen_port /*= 0 */,
	const char *pac_file /*= "unset"*/
	)
{
	char szUserPass[1000]={0};
	char *base64_userpass = NULL;

	g_terminate = 0;

	// ��username, password����base64����
	if( username != NULL )
	{
		snprintf(szUserPass,1000,"%s:%s",username?username:"",password?password:"" );
		base64_userpass = base64_encode( (const unsigned char *)szUserPass ,strlen( szUserPass ) );
	}

	// privoxy service already started?
	if( g_privoxy_service_stareted )
	{
		// δָ���¶˿�, ��ֱ��ʹ��֮ǰ�Ѿ�ʹ���еĶ˿�
		if( listen_port == 0 )
			listen_port = g_port_of_privoxy_listen ;
		else 
			g_port_of_privoxy_listen = listen_port;

		// just update the config of privoxy
		if( !wirte_parameters_to_config_file( 
			proxy_type,
			forward_socks_5_ip, 
			forward_socks5_port, 
			base64_userpass,
			listen_addr,
			listen_port,
			pac_file
			) ) 
		{
			freez( base64_userpass );
			return 3;
		}

		freez( base64_userpass );
		return 0;
	}

	if( !init_libprivoxy_path() ) return 1;

	g_port_of_privoxy_listen = listen_port;
	// Search avaible port?
	if( g_port_of_privoxy_listen == 0 )
	{
		g_port_of_privoxy_listen = SearchAnUnsedPort(DEFAULT_PRIVOXY_LISTEN_PORT, DEFAULT_SEARCH_AMOUNT );
		if( g_port_of_privoxy_listen == 0 ) return 2;
	}

	if( !wirte_parameters_to_config_file( 
		proxy_type,
		forward_socks_5_ip, 
		forward_socks5_port,
		base64_userpass,
		listen_addr,
		g_port_of_privoxy_listen,
		pac_file
		) )
	{
		freez( base64_userpass );
		return 3;
	}

	// start privoxy
	WinMain( NULL,NULL,NULL, 0);

	g_privoxy_service_stareted = true;

	freez( base64_userpass );

	return 0;
}

/** @brief ֹͣprivoxy
*/
extern "C" LIBPRIVOXY_API void __stdcall stop_privoxy()
{
	close_privoxy_listening_socket();

	g_terminate = 1;

	g_privoxy_service_stareted = false;
}

/** ���privoxy�������ĸ��˿�. һ������listen_port=0�������, ��֪��privoxy�������ĸ��˿�֮��
*/
extern "C" LIBPRIVOXY_API int __stdcall get_privoxy_port()
{
	return g_port_of_privoxy_listen;
}

extern "C" LIBPRIVOXY_API int __stdcall is_privoxy_started()
{
	return g_privoxy_service_stareted;
}

/** @brief ����cmd��hash ,�����¼����µ�cmd configʱ.
*/
extern "C" LIBPRIVOXY_API unsigned int __stdcall calc_cmd_hash( const char *cmd )
{
	unsigned int hash = hash_string( cmd );

	printf("cmd: %s hash: %u\n",cmd, hash );

	return  hash;
}

/** @biref ȡ��PAC��ַ
*/
extern "C" LIBPRIVOXY_API BOOL __stdcall GetPrivoxyPacUrl( char *buf, int buflen )
{
	assert( buf != NULL );
	assert( buflen > 0 );

	if( buf == NULL || buflen <= 0 || !g_privoxy_service_stareted ) return FALSE;

	char szPacTick[100] = {0};
	snprintf( szPacTick,100,("?t=%u"),GetTickCount() );

	snprintf( buf,buflen,("http://%s:%d/echo-pac%s"),
		DEFAULT_PRIVOXY_LISTEN_ADDR, 
		g_port_of_privoxy_listen,szPacTick );

	return TRUE;
}

/** @brief ȡ��PRIVOXY��ַ
*/
extern "C" LIBPRIVOXY_API BOOL __stdcall GetPrivoxyProxyAddr( char *buf, int buflen )
{
	assert( buf != NULL );
	assert( buflen > 0 );

	if( buf == NULL || buflen <= 0 || !g_privoxy_service_stareted ) return FALSE;

	snprintf( buf, buflen, _T("%s:%d"),DEFAULT_PRIVOXY_LISTEN_ADDR, g_port_of_privoxy_listen );

	return TRUE;
}