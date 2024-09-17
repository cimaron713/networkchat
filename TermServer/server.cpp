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

// ��ſ� ����� ����ü
// ���� ����
struct SOCKETINFO {
    SOCKET sock;
    char   buf[BUFSIZE];
    char   userID[20];
    int recvbytes;
    int sendbytes;
    bool recvdelayed;   // FD_READ �޼����� �޾�����, recv() �Լ��� ȣ������ ���� ���
    SOCKETINFO* next;
};
// �ۼ��� �޼��� ����
struct COMM_MSG {
    u_short type;
    u_short ip_addr_str;
    int  size;
};

// ��ſ� ����� ����
// ��Ƽ ĳ��Ʈ�� ����� ���� �ּ�
SOCKADDR_IN multicastRemoteAddr;
// ��ε� ĳ��Ʈ�� ����� ���� �ּ�
SOCKADDR_IN broadcastRemoteAddr;
// ���� ����
static SOCKET g_sockv4;
static SOCKET g_multicast_sock;
static SOCKET g_broadcast_sock;
// ��ſ� ����� �޼���
COMM_MSG comm_msg;
// ���� ���� ����Ʈ�� ������
SOCKETINFO* SocketInfoList;
// ��Ƽĳ��Ʈ �ּ�
char* multicastIPaddrString[] = { "235.7.8.9", "235.7.8.10", "235.7.8.11", "235.7.8.12" };
ULONG multicastIpAddr[MULTICAST_SIZE];

// ��ſ� ����� �޼���
// ��ȭ���� �Լ�
BOOL CALLBACK DlgProc(HWND, UINT, WPARAM, LPARAM);
// ������ �Լ�
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
void ProcessSocketMessage(HWND, UINT, WPARAM, LPARAM);
// ���� ���� �Լ�
BOOL AddSocketInfo(SOCKET sock, char* userID);
void RemoveSocketInfo(SOCKET sock);
SOCKETINFO* GetSocketInfo(SOCKET sock);
// ���� ��Ʈ�� ��� �Լ�
void DisplayText(char* fmt, ...);
// ä�ù� ���� �Լ�
void sendMessageToChatRoom(COMM_MSG* comm_msg, SOCKETINFO* ptr);
// �������� ���� �Լ�
void sendNotice(COMM_MSG* comm_msg, char* buf);
// ���� ��� �Լ�
void err_quit(char* msg);
void err_display(char* msg);
void err_display(int errcode);
// ���� �����Լ�
DWORD WINAPI ServerMain(LPVOID arg);

static HINSTANCE     g_hInst; // ���� ���α׷� �ν��Ͻ� �ڵ�
static HWND          g_hServerStateOutput;
static HWND          g_hDlg;
static HANDLE       g_hServerThread;

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    // ���� �ʱ�ȭ
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return 1;

    // ��ȭ���� ����
    g_hInst = hInstance;
    DialogBox(hInstance, MAKEINTRESOURCE(IDD_DIALOG1), NULL, (DLGPROC)&DlgProc);

    // ���� ����
    WSACleanup();
    return 0;
}

// ��ȭ���� ���ν���
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
     // ��ȭ���� ���� �޼��� ó��
    case WM_INITDIALOG:
        //��Ʈ��
        hShutdownUser = GetDlgItem(hDlg, IDC_SHUTDOWN_USER);
        hSendMsg = GetDlgItem(hDlg, IDC_SEND_MSG);
        hUserList = GetDlgItem(hDlg, IDC_SOCKET_LIST);
        hEditMsg = GetDlgItem(hDlg, IDC_EDIT1);
        hServerStateOutput = GetDlgItem(hDlg, IDC_SERVER_STATE);

        // ��Ʈ�� �ʱ�ȭ
        g_hServerThread = CreateThread(NULL, 0, ServerMain, NULL, 0, NULL);
        g_hServerStateOutput = hServerStateOutput;

        for (int i = 0; i < MULTICAST_SIZE; i++)
            multicastIpAddr[i] = inet_addr(multicastIPaddrString[i]);

        return TRUE;
     // ��ȭ���ڿ� �̺�Ʈ �߻����� �� �޼��� ó��
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
        // �������� ����
        case IDC_SEND_MSG: {
            COMM_MSG temp;
            char buf[1024];
            int type = 1000;
            memcpy(buf, (void*)&type, sizeof(int));
            strcpy(&buf[4], "����");
            GetDlgItemText(hDlg, IDC_EDIT1, &buf[24], 1024);

            temp.size = strlen(&buf[24]) + 1 + 20 + sizeof(int);    // null(1byte) + type(4bytes) + nickname(20) + data(24~) 
            temp.type = 1000;
            temp.ip_addr_str = 0x000F;      // 0.0.0.1111

            sendNotice(&temp, buf);
            return TRUE;
        }
        // ����
        case IDCANCEL:
            if (MessageBox(hDlg, (LPCSTR)"������ �����Ͻðڽ��ϱ�?", (LPCSTR)"����", MB_YESNO | MB_ICONQUESTION) == IDYES) {
                closesocket(g_sockv4);
                EndDialog(hDlg, IDCANCEL);
            }
            return TRUE;
        }
        return DefWindowProc(hDlg, uMsg, wParam, lParam);
    // ������ �߰� ���� ��
    case WM_ADDSOCKET: {
        SendMessage(hUserList, LB_ADDSTRING, 0, (LPARAM)lParam);
        return TRUE;
    }
    // ������ ���ŵ��� ��
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

// ������ ���ν���
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

    // ������ Ŭ���� ���
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

    // ������ ����
    HWND hWnd = CreateWindow("MyWndClass", "TCP ����", WS_OVERLAPPEDWINDOW,
        0, 0, 600, 200, NULL, NULL, NULL, NULL);
    if (hWnd == NULL) return 1;
    ShowWindow(hWnd, SW_SHOWNORMAL);
    UpdateWindow(hWnd);

    // ���� �ʱ�ȭ
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return 1;

    /*----- MULTICAST ���� ���� -----*/
    SOCKET sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock == INVALID_SOCKET) err_quit("socket()");

    // ��Ƽĳ��Ʈ TTL ����
    int ttl = 2;
    retval = setsockopt(sock, IPPROTO_IP, IP_MULTICAST_TTL, (char*)&ttl, sizeof(ttl));
    if (retval == SOCKET_ERROR) err_quit("setsockopt()");
    g_multicast_sock = sock;

    // multicast ���� �ּ� ����ü �ʱ�ȭ
    multicastRemoteAddr.sin_family = AF_INET;
    multicastRemoteAddr.sin_port = htons(REMOTEPORT);
    /*----- MULTICAST ���� ���� -----*/

    /*----- ��ε�ĳ���� ���� �ʱ�ȭ ���� -----*/
    // socket() - ���� ����
    SOCKET broadcastSock = socket(AF_INET, SOCK_DGRAM, 0);
    if (broadcastSock == INVALID_SOCKET) err_quit("socket()");

    // ��ε�ĳ���� Ȱ��ȭ
    BOOL bEnable = TRUE;
    retval = setsockopt(broadcastSock, SOL_SOCKET, SO_BROADCAST,
        (char*)&bEnable, sizeof(bEnable));
    if (retval == SOCKET_ERROR) err_quit("setsockopt()");

    // ���� �ּ� ����ü �ʱ�ȭ
    ZeroMemory(&broadcastRemoteAddr, sizeof(broadcastRemoteAddr));
    broadcastRemoteAddr.sin_family = AF_INET;
    broadcastRemoteAddr.sin_addr.s_addr = inet_addr(REMOTEIP);
    broadcastRemoteAddr.sin_port = htons(SERVERPORT);
    g_broadcast_sock = broadcastSock;
    /*----- ��ε�ĳ���� ���� �ʱ�ȭ �� -----*/

    /*----- ���� ���� �ʱ�ȭ ���� -----*/
    // socket() - ���� ����
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
    /*----- ���� ���� �ʱ�ȭ �� -----*/

    // WSASyncSelect()
    // retval = WSAAsyncSelect(listen_sock, hWnd, WM_SOCKET, FD_ACCEPT | FD_CLOSE);
    retval = WSAAsyncSelect(g_sockv4, hWnd, WM_SOCKET, FD_ACCEPT | FD_CLOSE);
    if (retval == SOCKET_ERROR)
        err_quit("WSAAsyncSelect()");

    // �޼����� �޾� ������ ���ν����� ����
    MSG msg;
    while (GetMessage(&msg, 0, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    // ���� ����
    WSACleanup();
    return msg.wParam;
}

// ���� ���� �޼��� ó��
void ProcessSocketMessage(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {

    // ������ ��ſ� ����� ����
    int retval;
    SOCKET client_sock;
    int addrlen, i, j;
    SOCKADDR_IN clientaddr;
    SOCKETINFO* ptr;

    // ���� �߻� ���� Ȯ��
    if (WSAGETSELECTERROR(lParam)) {
        err_display(WSAGETSELECTERROR(lParam));
        comm_msg.size = 0;
        comm_msg.type = 1000;
        comm_msg.ip_addr_str = 0x0000;
        RemoveSocketInfo(wParam);
        return;
    }

    // �޼��� ó��
    switch (WSAGETSELECTEVENT(lParam)) {
    case FD_ACCEPT:
        addrlen = sizeof(clientaddr);
        client_sock = accept(wParam, (SOCKADDR*)&clientaddr, &addrlen);
        if (client_sock == INVALID_SOCKET) {
            err_display("accept()");
            return;
        }
        retval = WSAAsyncSelect(client_sock, hWnd, WM_SOCKET, FD_READ | FD_WRITE | FD_CLOSE);
        DisplayText("Ŭ���̾�Ʈ ����: [%s]:%d\n", inet_ntoa(clientaddr.sin_addr), ntohs(clientaddr.sin_port));
        break;
    case FD_READ:
        // ���� �������� ������ ���
        if (GetSocketInfo(wParam) == NULL) {
            // ���� ���� �߰�
            char buf[20];
            retval = recv(wParam, buf, 20, 0);
            AddSocketInfo(wParam, buf);

            comm_msg.size = 5;
            comm_msg.type = 1000;
            comm_msg.ip_addr_str = 0x0000;

            // ������ �������� ����
            SendMessage(g_hDlg, WM_ADDSOCKET, (WPARAM)wParam, (LPARAM)buf);
            break;
        }
        // �������� �̹� ������ - ��� 
        else {
            ptr = GetSocketInfo(wParam);
            if (ptr->recvbytes > 0) {
                ptr->recvdelayed = TRUE;
                return;
            }

            // ������ ũ�� �ޱ�
            retval = recv(ptr->sock, (char*)&comm_msg, sizeof(COMM_MSG), 0);
            if (retval == 0 || retval == SOCKET_ERROR) {
                RemoveSocketInfo(wParam);
            }
            // ������ �ޱ�
            retval = recv(ptr->sock, ptr->buf, comm_msg.size, 0);
            if (retval == 0 || retval == SOCKET_ERROR) {
                RemoveSocketInfo(wParam);
            }

            // ������ ���� ũ�� ����
            ptr->recvbytes = retval;
            ptr->buf[retval] = '\0';
        }
    case FD_WRITE:
        ptr = GetSocketInfo(wParam);
        if (ptr != NULL) {
            // ä�ù� ����
            sendMessageToChatRoom(&comm_msg, ptr);
        }
        break;
    case FD_CLOSE:
        RemoveSocketInfo(wParam);
        break;
    }
}


// ä�ù� ����
void sendMessageToChatRoom(COMM_MSG* comm_msg, SOCKETINFO* ptr) {
    int retval, j;

    for (int i = 0; i < MULTICAST_SIZE; i++) {
        // ��Ʈ������ ���� ä�ù� �ĺ�
        if (comm_msg->ip_addr_str & (0x0001 << i)) {   
            multicastRemoteAddr.sin_addr.s_addr = multicastIpAddr[i];

            // ������ ũ�� ����
            retval = sendto(g_multicast_sock, (char*)comm_msg, sizeof(COMM_MSG), 0, (sockaddr*)&multicastRemoteAddr, sizeof(SOCKADDR_IN));
            if (retval == SOCKET_ERROR) {
                err_quit("sendto1()");
            }

            // ������ ����
            retval = sendto(g_multicast_sock, ptr->buf + ptr->sendbytes, ptr->recvbytes - ptr->sendbytes, 0, (SOCKADDR*)&multicastRemoteAddr, sizeof(SOCKADDR_IN));
            if (retval == SOCKET_ERROR) {
                err_quit("sendto2()");
            }

            // ���� ������ ����Ʈ�� ����
            ptr->sendbytes += retval;
            // ���� �����͸� ��� ���´��� üũ
            if (ptr->recvbytes == ptr->sendbytes) {
                ptr->recvbytes = 0;
                ptr->sendbytes = 0;
                if (ptr->recvdelayed) {
                    ptr->recvdelayed = FALSE;
                    PostMessage(g_hDlg, WM_SOCKET, ptr->sock, FD_READ);     // �������� �ʱ�ȭ
                }
            }
        }
    }
}

// �������� ����
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

// ���� ���� �߰�
BOOL AddSocketInfo(SOCKET sock, char* userID)
{
    SOCKETINFO* ptr = new SOCKETINFO;
    if (ptr == NULL) {
        printf("[����] �޸𸮰� �����մϴ�!\n");
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

// ���� ���� ���
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

// ���� ���� ����
void RemoveSocketInfo(SOCKET sock)
{
    SOCKADDR_IN clientaddr;
    int addrlen = sizeof(clientaddr);
    getpeername(sock, (SOCKADDR*)&clientaddr, &addrlen);
    DisplayText("[TCP ����] Ŭ���̾�Ʈ ����: IP �ּ�=%s, ��Ʈ ��ȣ=%d\n",
        inet_ntoa(clientaddr.sin_addr), ntohs(clientaddr.sin_port));

    // ���Ḯ��Ʈ���� ���� ����
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

// ä�� ���
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

// ���� �Լ� ���� ��� �� ����
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

// ���� �Լ� ���� ���
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

// ���� �Լ� ���� ���
void err_display(int errcode)
{
    LPVOID lpMsgBuf;
    FormatMessage(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
        NULL, errcode,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        (LPTSTR)&lpMsgBuf, 0, NULL);
    printf("[����] %s", (char*)lpMsgBuf);
    LocalFree(lpMsgBuf);
}