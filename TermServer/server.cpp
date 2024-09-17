#pragma comment(lib, "ws2_32")
#include <winsock2.h>
#include <ws2tcpip.h>
#include <Windows.h>
#include <stdlib.h>
#include <stdio.h>
#include <commctrl.h>
#include "resources.h"
#include <tchar.h>

#define MULTICAST_SIZE 4
#define MULTICAST_TTL 2
#define REMOTEPORT  9000
#define SERVERPORT 9000
#define BUFSIZE    256
#define WM_SOCKET (WM_USER+1)
#define WM_ADDSOCKET (WM_USER+1)
#define WM_RMSOCKET  (WM_USER+2)
#define SERVER_MESSAGE 1111
#define REMOTEIP "255.255.255.255"

// 통신에 사용할 구조체
// 소켓 정보
struct SOCKETINFO {
    SOCKET sock;
    char   buf[BUFSIZE];
    char   userID[20];
    int recvbytes;
    int sendbytes;
    bool recvdelayed;   // FD_READ 메세지를 받았지만, recv() 함수를 호출하지 않은 경우
    SOCKETINFO* next;
};
// 송수신 메세지 정보
struct COMM_MSG {
    u_short type;
    u_short ip_addr_str;
    int  size;
};

// 통신에 사용할 변수
// 멀티 캐스트에 사용할 소켓 주소
SOCKADDR_IN multicastRemoteAddr;
// 브로드 캐스트에 사용할 소켓 주소
SOCKADDR_IN broadcastRemoteAddr;
// 서버 소켓
static SOCKET g_sockv4;
static SOCKET g_multicast_sock;
static SOCKET g_broadcast_sock;
// 통신에 사용할 메세지
COMM_MSG comm_msg;
// 단일 연결 리스트의 시작점
SOCKETINFO* SocketInfoList;
// 멀티캐스트 주소
char* multicastIPaddrString[] = { "235.7.8.9", "235.7.8.10", "235.7.8.11", "235.7.8.12" };
ULONG multicastIpAddr[MULTICAST_SIZE];

// 통신에 사용할 메서드
// 대화상자 함수
BOOL CALLBACK DlgProc(HWND, UINT, WPARAM, LPARAM);
// 윈도우 함수
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
void ProcessSocketMessage(HWND, UINT, WPARAM, LPARAM);
// 소켓 관리 함수
BOOL AddSocketInfo(SOCKET sock, char* userID);
void RemoveSocketInfo(SOCKET sock);
SOCKETINFO* GetSocketInfo(SOCKET sock);
// 편집 컨트롤 출력 함수
void DisplayText(char* fmt, ...);
// 채팅방 전송 함수
void sendMessageToChatRoom(COMM_MSG* comm_msg, SOCKETINFO* ptr);
// 공지사항 전송 함수
void sendNotice(COMM_MSG* comm_msg, char* buf);
// 오류 출력 함수
void err_quit(char* msg);
void err_display(char* msg);
void err_display(int errcode);
// 서버 메인함수
DWORD WINAPI ServerMain(LPVOID arg);

static HINSTANCE     g_hInst; // 응용 프로그램 인스턴스 핸들
static HWND          g_hServerStateOutput;
static HWND          g_hDlg;
static HANDLE       g_hServerThread;

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    // 윈속 초기화
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return 1;

    // 대화상자 생성
    g_hInst = hInstance;
    DialogBox(hInstance, MAKEINTRESOURCE(IDD_DIALOG1), NULL, (DLGPROC)&DlgProc);

    // 윈속 종료
    WSACleanup();
    return 0;
}

// 대화상자 프로시저
BOOL CALLBACK DlgProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    static HWND hShutdownUser;
    static HWND hSendMsg;
    static HWND hUserList;
    static HWND hEditMsg;
    static HWND hServerStateOutput;
    g_hDlg = hDlg;
    LVITEM user;
    memset(&user, 0, sizeof(user));
    user.mask = LVCF_TEXT;
    user.cchTextMax = 20;

    switch (uMsg) {
     // 대화상자 생성 메세지 처리
    case WM_INITDIALOG:
        //컨트롤
        hShutdownUser = GetDlgItem(hDlg, IDC_SHUTDOWN_USER);
        hSendMsg = GetDlgItem(hDlg, IDC_SEND_MSG);
        hUserList = GetDlgItem(hDlg, IDC_SOCKET_LIST);
        hEditMsg = GetDlgItem(hDlg, IDC_EDIT1);
        hServerStateOutput = GetDlgItem(hDlg, IDC_SERVER_STATE);

        // 컨트롤 초기화
        g_hServerThread = CreateThread(NULL, 0, ServerMain, NULL, 0, NULL);
        g_hServerStateOutput = hServerStateOutput;

        for (int i = 0; i < MULTICAST_SIZE; i++)
            multicastIpAddr[i] = inet_addr(multicastIPaddrString[i]);

        return TRUE;
     // 대화상자에 이벤트 발생했을 때 메세지 처리
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDC_SHUTDOWN_USER: {
            char buf[BUFSIZ];
            SOCKETINFO* ptr = SocketInfoList;
            int i = SendMessage(hUserList, LB_GETCURSEL, 0, 0);
            SendMessage(hUserList, LB_GETTEXT, i, (LPARAM)buf);
            if (strlen(buf) != 0) {
                while (ptr) {
                    if (strcmp(ptr->userID, buf) == 0) {
                        RemoveSocketInfo(ptr->sock);
                        break;
                    }
                    ptr = ptr->next;
                }
            }
            return TRUE;
        }
        // 공지사항 전송
        case IDC_SEND_MSG: {
            COMM_MSG temp;
            char buf[1024];
            int type = 1000;
            memcpy(buf, (void*)&type, sizeof(int));
            strcpy(&buf[4], "서버");
            GetDlgItemText(hDlg, IDC_EDIT1, &buf[24], 1024);

            temp.size = strlen(&buf[24]) + 1 + 20 + sizeof(int);    // null(1byte) + type(4bytes) + nickname(20) + data(24~) 
            temp.type = 1000;
            temp.ip_addr_str = 0x000F;      // 0.0.0.1111

            sendNotice(&temp, buf);
            return TRUE;
        }
        // 종료
        case IDCANCEL:
            if (MessageBox(hDlg, (LPCSTR)"정말로 종료하시겠습니까?", (LPCSTR)"질문", MB_YESNO | MB_ICONQUESTION) == IDYES) {
                closesocket(g_sockv4);
                EndDialog(hDlg, IDCANCEL);
            }
            return TRUE;
        }
        return DefWindowProc(hDlg, uMsg, wParam, lParam);
    // 소켓이 추가 됐을 때
    case WM_ADDSOCKET: {
        SendMessage(hUserList, LB_ADDSTRING, 0, (LPARAM)lParam);
        return TRUE;
    }
    // 소켓이 제거됐을 때
    case WM_RMSOCKET: {
        int index = SendMessage(hUserList, LB_FINDSTRINGEXACT, -1, (LPARAM)wParam);
        SendMessage(hUserList, LB_DELETESTRING, index, 0);
        return TRUE;
    }
    default:
        return DefWindowProc(hDlg, uMsg, wParam, lParam);
    }
    return FALSE;
}

// 윈도우 프로시저
LRESULT CALLBACK WndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_SOCKET:
        ProcessSocketMessage(hWnd, uMsg, wParam, lParam);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hWnd, uMsg, wParam, lParam);
}

DWORD WINAPI ServerMain(LPVOID arg) {
    int retval;

    // 윈도우 클래스 등록
    WNDCLASS wndclass;
    wndclass.style = CS_HREDRAW | CS_VREDRAW;
    wndclass.lpfnWndProc = WndProc;
    wndclass.cbClsExtra = 0;
    wndclass.cbWndExtra = 0;
    wndclass.hInstance = NULL;
    wndclass.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wndclass.hCursor = LoadCursor(NULL, IDC_ARROW);
    wndclass.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
    wndclass.lpszMenuName = NULL;
    wndclass.lpszClassName = "MyWndClass";
    if (!RegisterClass(&wndclass)) return 1;

    // 윈도우 생성
    HWND hWnd = CreateWindow("MyWndClass", "TCP 서버", WS_OVERLAPPEDWINDOW,
        0, 0, 600, 200, NULL, NULL, NULL, NULL);
    if (hWnd == NULL) return 1;
    ShowWindow(hWnd, SW_SHOWNORMAL);
    UpdateWindow(hWnd);

    // 윈속 초기화
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return 1;

    /*----- MULTICAST 소켓 설정 -----*/
    SOCKET sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock == INVALID_SOCKET) err_quit("socket()");

    // 멀티캐스트 TTL 설정
    int ttl = 2;
    retval = setsockopt(sock, IPPROTO_IP, IP_MULTICAST_TTL, (char*)&ttl, sizeof(ttl));
    if (retval == SOCKET_ERROR) err_quit("setsockopt()");
    g_multicast_sock = sock;

    // multicast 보낼 주소 구조체 초기화
    multicastRemoteAddr.sin_family = AF_INET;
    multicastRemoteAddr.sin_port = htons(REMOTEPORT);
    /*----- MULTICAST 소켓 설정 -----*/

    /*----- 브로드캐스팅 소켓 초기화 시작 -----*/
    // socket() - 소켓 생성
    SOCKET broadcastSock = socket(AF_INET, SOCK_DGRAM, 0);
    if (broadcastSock == INVALID_SOCKET) err_quit("socket()");

    // 브로드캐스팅 활성화
    BOOL bEnable = TRUE;
    retval = setsockopt(broadcastSock, SOL_SOCKET, SO_BROADCAST,
        (char*)&bEnable, sizeof(bEnable));
    if (retval == SOCKET_ERROR) err_quit("setsockopt()");

    // 소켓 주소 구조체 초기화
    ZeroMemory(&broadcastRemoteAddr, sizeof(broadcastRemoteAddr));
    broadcastRemoteAddr.sin_family = AF_INET;
    broadcastRemoteAddr.sin_addr.s_addr = inet_addr(REMOTEIP);
    broadcastRemoteAddr.sin_port = htons(SERVERPORT);
    g_broadcast_sock = broadcastSock;
    /*----- 브로드캐스팅 소켓 초기화 끝 -----*/

    /*----- 서버 소켓 초기화 시작 -----*/
    // socket() - 소켓 생성
    SOCKET listen_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_sock == INVALID_SOCKET) err_quit("socket()");

    g_sockv4 = listen_sock;

    // bind()
    SOCKADDR_IN serveraddr;
    memset(&serveraddr, 0, sizeof(serveraddr));
    serveraddr.sin_family = AF_INET;
    serveraddr.sin_addr.s_addr = htonl(INADDR_ANY);
    serveraddr.sin_port = htons(SERVERPORT);
    retval = bind(listen_sock, (SOCKADDR*)&serveraddr, sizeof(serveraddr));
    if (retval == SOCKET_ERROR) err_quit("bind()");

    // listen()
    retval = listen(listen_sock, SOMAXCONN);
    if (retval == SOCKET_ERROR) err_quit("listen()");
    /*----- 서버 소켓 초기화 끝 -----*/

    // WSASyncSelect()
    // retval = WSAAsyncSelect(listen_sock, hWnd, WM_SOCKET, FD_ACCEPT | FD_CLOSE);
    retval = WSAAsyncSelect(g_sockv4, hWnd, WM_SOCKET, FD_ACCEPT | FD_CLOSE);
    if (retval == SOCKET_ERROR)
        err_quit("WSAAsyncSelect()");

    // 메세지를 받아 윈도우 프로시저로 전송
    MSG msg;
    while (GetMessage(&msg, 0, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    // 윈속 종료
    WSACleanup();
    return msg.wParam;
}

// 소켓 관련 메세지 처리
void ProcessSocketMessage(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {

    // 데이터 통신에 사용할 변수
    int retval;
    SOCKET client_sock;
    int addrlen, i, j;
    SOCKADDR_IN clientaddr;
    SOCKETINFO* ptr;

    // 오류 발생 여부 확인
    if (WSAGETSELECTERROR(lParam)) {
        err_display(WSAGETSELECTERROR(lParam));
        comm_msg.size = 0;
        comm_msg.type = 1000;
        comm_msg.ip_addr_str = 0x0000;
        RemoveSocketInfo(wParam);
        return;
    }

    // 메세지 처리
    switch (WSAGETSELECTEVENT(lParam)) {
    case FD_ACCEPT:
        addrlen = sizeof(clientaddr);
        client_sock = accept(wParam, (SOCKADDR*)&clientaddr, &addrlen);
        if (client_sock == INVALID_SOCKET) {
            err_display("accept()");
            return;
        }
        retval = WSAAsyncSelect(client_sock, hWnd, WM_SOCKET, FD_READ | FD_WRITE | FD_CLOSE);
        DisplayText("클라이언트 접속: [%s]:%d\n", inet_ntoa(clientaddr.sin_addr), ntohs(clientaddr.sin_port));
        break;
    case FD_READ:
        // 기존 소켓정보 없으면 등록
        if (GetSocketInfo(wParam) == NULL) {
            // 소켓 정보 추가
            char buf[20];
            retval = recv(wParam, buf, 20, 0);
            AddSocketInfo(wParam, buf);

            comm_msg.size = 5;
            comm_msg.type = 1000;
            comm_msg.ip_addr_str = 0x0000;

            // 접속한 유저정보 전달
            SendMessage(g_hDlg, WM_ADDSOCKET, (WPARAM)wParam, (LPARAM)buf);
            break;
        }
        // 소켓정보 이미 있으면 - 통신 
        else {
            ptr = GetSocketInfo(wParam);
            if (ptr->recvbytes > 0) {
                ptr->recvdelayed = TRUE;
                return;
            }

            // 데이터 크기 받기
            retval = recv(ptr->sock, (char*)&comm_msg, sizeof(COMM_MSG), 0);
            if (retval == 0 || retval == SOCKET_ERROR) {
                RemoveSocketInfo(wParam);
            }
            // 데이터 받기
            retval = recv(ptr->sock, ptr->buf, comm_msg.size, 0);
            if (retval == 0 || retval == SOCKET_ERROR) {
                RemoveSocketInfo(wParam);
            }

            // 데이터 받은 크기 저장
            ptr->recvbytes = retval;
            ptr->buf[retval] = '\0';
        }
    case FD_WRITE:
        ptr = GetSocketInfo(wParam);
        if (ptr != NULL) {
            // 채팅방 전송
            sendMessageToChatRoom(&comm_msg, ptr);
        }
        break;
    case FD_CLOSE:
        RemoveSocketInfo(wParam);
        break;
    }
}


// 채팅방 전송
void sendMessageToChatRoom(COMM_MSG* comm_msg, SOCKETINFO* ptr) {
    int retval, j;

    for (int i = 0; i < MULTICAST_SIZE; i++) {
        // 비트연산을 통한 채팅방 식별
        if (comm_msg->ip_addr_str & (0x0001 << i)) {   
            multicastRemoteAddr.sin_addr.s_addr = multicastIpAddr[i];

            // 데이터 크기 전송
            retval = sendto(g_multicast_sock, (char*)comm_msg, sizeof(COMM_MSG), 0, (sockaddr*)&multicastRemoteAddr, sizeof(SOCKADDR_IN));
            if (retval == SOCKET_ERROR) {
                err_quit("sendto1()");
            }

            // 데이터 전송
            retval = sendto(g_multicast_sock, ptr->buf + ptr->sendbytes, ptr->recvbytes - ptr->sendbytes, 0, (SOCKADDR*)&multicastRemoteAddr, sizeof(SOCKADDR_IN));
            if (retval == SOCKET_ERROR) {
                err_quit("sendto2()");
            }

            // 보낸 데이터 바이트수 갱신
            ptr->sendbytes += retval;
            // 받은 데이터를 모두 보냈는지 체크
            if (ptr->recvbytes == ptr->sendbytes) {
                ptr->recvbytes = 0;
                ptr->sendbytes = 0;
                if (ptr->recvdelayed) {
                    ptr->recvdelayed = FALSE;
                    PostMessage(g_hDlg, WM_SOCKET, ptr->sock, FD_READ);     // 소켓정보 초기화
                }
            }
        }
    }
}

// 공지사항 전송
void sendNotice(COMM_MSG* comm_msg, char* buf) {
    int retval;
    retval = sendto(g_broadcast_sock, (char*)comm_msg, sizeof(COMM_MSG), 0, (sockaddr*)&broadcastRemoteAddr, sizeof(SOCKADDR_IN));
    if (retval == SOCKET_ERROR) {
        err_quit("sendto()");
    }
    retval = sendto(g_broadcast_sock, buf, comm_msg->size, 0, (sockaddr*)&broadcastRemoteAddr, sizeof(SOCKADDR_IN));
    if (retval == SOCKET_ERROR) {
        err_quit("sendto()");
    }
}

// 소켓 정보 추가
BOOL AddSocketInfo(SOCKET sock, char* userID)
{
    SOCKETINFO* ptr = new SOCKETINFO;
    if (ptr == NULL) {
        printf("[오류] 메모리가 부족합니다!\n");
        return FALSE;
    }

    ptr->sock = sock;
    ptr->recvbytes = 0;
    ptr->sendbytes = 0;
    ptr->recvdelayed = FALSE;
    ptr->next = SocketInfoList;
    strcpy(ptr->userID, userID);
    strcpy(ptr->buf, "TEMP");

    SocketInfoList = ptr;

    return TRUE;
}

// 소켓 정보 얻기
SOCKETINFO* GetSocketInfo(SOCKET sock)
{
    SOCKETINFO* ptr = SocketInfoList;

    while (ptr) {
        if (ptr->sock == sock)
            return ptr;
        ptr = ptr->next;
    }

    return NULL;
}

// 소켓 정보 제거
void RemoveSocketInfo(SOCKET sock)
{
    SOCKADDR_IN clientaddr;
    int addrlen = sizeof(clientaddr);
    getpeername(sock, (SOCKADDR*)&clientaddr, &addrlen);
    DisplayText("[TCP 서버] 클라이언트 종료: IP 주소=%s, 포트 번호=%d\n",
        inet_ntoa(clientaddr.sin_addr), ntohs(clientaddr.sin_port));

    // 연결리스트에서 정보 제거
    SOCKETINFO* curr = SocketInfoList;
    SOCKETINFO* prev = NULL;

    while (curr) {
        if (curr->sock == sock) {
            SendMessage(g_hDlg, WM_RMSOCKET, (WPARAM)curr->userID, 0);
            if (prev)
                prev->next = curr->next;
            else
                SocketInfoList = curr->next;
            closesocket(curr->sock);
            delete curr;
            return;
        }
        prev = curr;
        curr = curr->next;
    }
}

// 채팅 출력
void DisplayText(char* fmt, ...) {
    va_list arg;
    va_start(arg, fmt);

    char cbuf[1024];
    vsprintf(cbuf, fmt, arg);

    int nLength = GetWindowTextLength(g_hServerStateOutput);
    SendMessage(g_hServerStateOutput, EM_SETSEL, nLength, nLength);
    SendMessage(g_hServerStateOutput, EM_REPLACESEL, FALSE, (LPARAM)cbuf);

    va_end(arg);
}

// 소켓 함수 오류 출력 후 종료
void err_quit(char* msg)
{
    LPVOID lpMsgBuf;
    FormatMessage(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
        NULL, WSAGetLastError(),
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        (LPTSTR)&lpMsgBuf, 0, NULL);
    MessageBox(NULL, (LPCTSTR)lpMsgBuf, msg, MB_ICONERROR);
    LocalFree(lpMsgBuf);
    exit(1);
}

// 소켓 함수 오류 출력
void err_display(char* msg)
{
    LPVOID lpMsgBuf;
    FormatMessage(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
        NULL, WSAGetLastError(),
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        (LPTSTR)&lpMsgBuf, 0, NULL);
    printf("[%s] %s", msg, (char*)lpMsgBuf);
    LocalFree(lpMsgBuf);
}

// 소켓 함수 오류 출력
void err_display(int errcode)
{
    LPVOID lpMsgBuf;
    FormatMessage(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
        NULL, errcode,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        (LPTSTR)&lpMsgBuf, 0, NULL);
    printf("[오류] %s", (char*)lpMsgBuf);
    LocalFree(lpMsgBuf);
}