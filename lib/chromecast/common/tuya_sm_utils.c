#include <sys/time.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <stdarg.h>
#include <time.h>
#include <netdb.h>
#include <string.h>
#include <stdio.h>
#if defined(__LITEOS__) || defined(__ANDROID__)
#else
#include <sys/timeb.h>
#endif

#if 0
int tuya_parse_domain_ip_address(const char *doname, char* ipAddr)
{
    char** pptr = NULL;
    struct hostent *hptr = NULL;
    char   str[32] = {0};

    if((hptr = gethostbyname(doname)) == NULL) {
        printf("host: %s\n", doname);
        return -1;
    }

    switch(hptr->h_addrtype) {
    case AF_INET:
    case AF_INET6:
        pptr = hptr->h_addr_list;
        for(; *pptr != NULL; pptr++) {
            strcpy(ipAddr, inet_ntop(hptr->h_addrtype, *pptr, str, sizeof(str)));
            return 0;
        }
        break;

    default:
        break;
    }

    return -1;
}

// rtsp://rtsp.tuyacn.com:18554/v1/proxy/ipc/2812987377548101079
// rtsp://username:password@domain:port/v/xxx/xxx
// Parse the URL as "rtsp://[<username>[:<password>]@]<server-address-or-name>[:<port>][/<path>]"
int tuya_parse_rtsp_url_info(char const* url, char* username, char* password, char* address, int* portNum, char* path)
{
    uint32_t const prefixLength = 7;  
    char const* from = &url[prefixLength];      
    char const* tmpPos = NULL;  
  
    if ((tmpPos = strchr(from, '@')) != NULL) {  
        // found <username> (and perhaps <password>).  
        char const* usernameStart = from;  
        char const* passwordStart = NULL;  
        char const* p = tmpPos;  
  
        if ((tmpPos = strchr(from, ':')) != NULL && tmpPos < p) {  
            passwordStart = tmpPos + 1;
            uint32_t passwordLen = p - passwordStart;  
            strncpy(password, passwordStart, passwordLen);
            password[passwordLen] = '\0'; // Set the ending character.  
        }  
              
        uint32_t usernameLen = 0;  
        if (passwordStart != NULL) {
            usernameLen = tmpPos - usernameStart;  
        } else {  
            usernameLen = p - usernameStart;      
        }         
        strncpy(username, usernameStart, usernameLen);
        username[usernameLen] = '\0';  // Set the ending character.  
  
        from = p + 1; // skip the '@'  
    }  
  
    const char* pathStart = NULL;  
    if ((tmpPos = strchr(from, '/')) != NULL) {  
        uint32_t pathLen = strlen(tmpPos + 1);  //Skip '/'  
        strncpy(path, tmpPos + 1, pathLen + 1);  
        pathStart = tmpPos;  
    }  
  
    // Next, will parse the address and port.  
    tmpPos = strchr(from, ':');  
    if (tmpPos == NULL) {  
        if (pathStart == NULL) {  
            uint32_t addressLen = strlen(from);  
            strncpy(address, from, addressLen + 1);  //Already include '\0'  
        } else {  
            uint32_t addressLen = pathStart - from;  
            strncpy(address, from, addressLen);  
            address[addressLen] = '\0';   //Set the ending character.  
        }

        *portNum = 554; // Has not the specified port, and will use the default value  
    } else if (tmpPos != NULL) {  
        uint32_t addressLen = tmpPos - from;
        strncpy(address, from, addressLen);  
        address[addressLen] = '\0';  //Set the ending character.  
        *portNum = strtoul(tmpPos + 1, NULL, 10);   
    }

    if(strlen(address) <= 0) {
        return -1;
    }

    return 0;
}

void tuya_setup_rtsp_option(const char* rtspUrl, char* pOptionStr)
{
    char *tmp = NULL;

    tmp = strstr((char *)rtspUrl, "@");
    if(tmp != NULL) {
        strcpy(pOptionStr, "OPTIONS rtsp://");
        strcat(pOptionStr, tmp + 1);
    } else {
        strcpy(pOptionStr, "OPTIONS ");
        strcat(pOptionStr, rtspUrl);
    }

    strcat(pOptionStr, " RTSP/1.0\r\nCseq: 27\r\nUser-Agent: VLC Media Player (LIVE.COM Streaming Media v2004.11.11)\r\n\r\n");

    return;
}
#endif

uint64_t tuya_flv_time_utc_sec()
{
    struct timeval now;

    gettimeofday(&now, NULL);

    return now.tv_sec;
}

uint64_t tuya_time_utc_ms()
{
    struct timeval now;

    gettimeofday(&now, NULL);

    return now.tv_sec * 1000 + now.tv_usec / 1000;
}

unsigned char* get_one_nalu(unsigned char *pBufIn, int nInSize, int* nNaluSize)
{
    u_char *p = pBufIn;
    int nStartPos = 0, nEndPos = 0;

    if(pBufIn == NULL || nInSize <= 0 || nNaluSize == NULL) {
        return NULL;
    }

    while (1) {
        if (p[0] == 0 && p[1] == 0 && p[2] == 0 && p[3] == 1) {
            nStartPos = p - pBufIn;
            break;
        }

        p++;

        if (p - pBufIn >= nInSize - 4) {
            return NULL;
        }
    }

    p++;

    while (1) {
        if (p[0] == 0 && p[1] == 0 && p[2] == 0 && p[3] == 1) {
            nEndPos = p - pBufIn;
            break;
        }

        p++;

        if (p - pBufIn >= nInSize - 4) {
            nEndPos = nInSize;
            break;
        }
    }

    *nNaluSize = nEndPos - nStartPos;

    return pBufIn + nStartPos;
}

void tuya_log_print(const char* params, const char *fmt, ...)
{
#if defined(__LITEOS__) || defined(__ANDROID__)
    return;
#else
    FILE *fp = stderr;
    va_list args;
    struct tm *t_info = NULL;
    struct timeb stTimeb;

    ftime(&stTimeb);
    t_info = localtime(&stTimeb.time);

    fprintf(fp, "[%d-%d-%d %d:%d:%d.%d] %s ", t_info->tm_year + 1900, t_info->tm_mon + 1,
                t_info->tm_mday, t_info->tm_hour, t_info->tm_min, t_info->tm_sec, stTimeb.millitm, params);

    va_start(args, fmt);
    vfprintf(fp, fmt, args);
    va_end(args);
    return;
#endif
}

