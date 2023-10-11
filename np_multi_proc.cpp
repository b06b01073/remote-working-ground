#include<bits/stdc++.h>
#include <regex>
#include<unistd.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/wait.h>
#include<sys/socket.h>
#include<netinet/in.h> // sockaddr_in
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/ipc.h>
#include <sys/shm.h>


#define BUFFERSIZE          15005
#define CLIENT_EXIT         100
#define CLIENT_STAY         101
#define LOGIN               102
#define LOGOUT              103
#define NAME                104
#define YELL                105
#define PIPEOUT_OK          106
#define USER_NOTEXIST       107
#define PIPE_EXISTED        108
#define PIPE_NOT_EXISTED    109
#define PIPEIN_OK           110
#define PIPEIN              111
#define PIPEOUT             112
#define MAX_CLIENT          40
#define NULL_ID             -1
#define NULL_FD             -2
#define TELL                113


using namespace std;

#define ll long long

// note: 關於error message與%的先後順序https://e3.nycu.edu.tw/mod/dcpcforum/discuss.php?d=206417

ll timeStamp = 0;   

struct Client {
    int id;

    // note that string is bad here, it allocate memory dynamically, which break the pointer
    char name[2000];
    int clientSockFd;
    in_addr address;
    u_short port;
    char news[BUFFERSIZE];
    int pid;
    int mailBox[MAX_CLIENT + 5];
};


struct Pipe {
    int readEnd;
    int writeEnd;

    // type is a reserved word
    string Type; 
    int countdown;
};

vector<Pipe> timer;


// use regex to match the numbered pipe operator
regex numberedPipe("\\|[0-9]+");
regex errorPipe("\\![0-9]+");
regex userPipeOut(">[0-9]+");
regex userPipeIn("<[0-9]+");

void npshell(int);
vector<string> parseCommand(string);
vector<string> extractCommand(const vector<string>&, int&, int&, int&, int&);
void processGetenv(const vector<string>&, int);
void processSetenv(const vector<string>&, int);
void openPipe(string& Type);
void processCommand(char**, int&, int&, int&, string&, int);
void redirectIO(int&, int&, int&, string&, int);
char** getArgs(const vector<string>&);
vector<string> getCommandPaths(const string&);
void tick(bool, bool);
bool pipeNotExist(string&);
void checkIO(const int, const int, const int);
bool checkCommand(string);
bool canOpen(string);
int getIdFromPool();
vector<string> modifyCommand(vector<string>);

void printWelcome(int);

int initSocket(int);
void initSockAddrIn(struct sockaddr_in&, int);

void processRequest(int);
void broadcast(int, int, string="", int=-1);

void cleaning(int);

void initClientMem();
void initShareMem();
void initClient(int);

void processWho(int);
void processTell(const vector<string>&, int);
void processYell(const vector<string>&, int);
void processName(const vector<string>&, int);

void cleanUpClient();

string getNameString(int);
string getLoginString(int);
string getLogOutString(int);
string getUserNotExistString(int);
string getYellString(int, string);
string getTellString(int, string);
string getPipeInString(int, int, string);
string getPipeOutString(int, int, string);
string getPipeExistedString(int, int);
string getPipeNotExistedString(int, int);
void broadcasting(int);

Client* getClientById(int);


Client* startAddr;      // the start address of the shared memory    
Client* clientAddr;     // the addr of the current client;

void cleanUpPipe();
int checkUserPipeOut(int, int);
int checkUserPipeIn(int, int);
void initMkfifo();
string fifoName(int, int);

bool isMailBoxOpen(int, int);
void setMailBox(int, int);
void unsetMailBox(int, int);

void printMailBox(int);

// ./np_simple_proc port_number
int main(int argc, char* const argv[]) {
    if(argc != 2) {
        cerr << "You need to execute the process by: ./np_simple_proc port_number" << endl;
        return -1;
    }

    initMkfifo();

    // set the initial PATH environment variable;
    setenv("PATH", "bin:.", 1);

    // init the socket here
    int portNumber = atoi(argv[1]);
    processRequest(portNumber);
}


// npshell need to write to clientSockFd by calling write instead of cout now
void npshell(int clientSockFd) {
    cout << "% ";

    string rawCommand;
    while(getline(cin, rawCommand)) {
        // pop back the '\n', otherwise it will cause a "really really really really" horrible bug in getPipeOutString
        if(rawCommand.back() == '\n') {
            rawCommand.pop_back();
        }

        // the parseCommand will also strip the ending carriage return 
        vector<string> parsedCommand = parseCommand(rawCommand);
        vector<string> modifiedCommand = modifyCommand(parsedCommand);

        // cout << "modified command: ";
        // for(auto& c: modifiedCommand)
            // cout << c << " ";
        // cout << '\n';

        // the child pids, only wait when the command is not a numbered pipe(since it need to be finished now).
        vector<int> cpids;

        // no input
        if(parsedCommand.size() == 0){
            // cout << "% ";
            write(clientSockFd, "% ", strlen("% "));
            continue;
        }

        int idx = 0;
        bool shouldWait = true;

        while(idx < modifiedCommand.size()) {
            // cout << '\n';
            // IO fd, the output is send to the client
            int input = -1;
            int output = -1;
            int errOutput = -1;

            vector<string> extractedCommand = extractCommand(modifiedCommand, idx, input, output, errOutput);
            string lastCommand = extractedCommand.back();
            string firstCommand = extractedCommand[0];


            // cout << "firstCommand: " << firstCommand << ", lastCommand: " << lastCommand << '\n';

            // operator is not considered as a part of command in my implementation
            if(lastCommand == "|" || lastCommand == ">" || regex_match(lastCommand, numberedPipe) || regex_match(lastCommand, errorPipe) || regex_match(lastCommand, userPipeIn) || regex_match(lastCommand, userPipeOut)) {
                extractedCommand.pop_back();
            }
            

            
            // up to here the default i, o, err = stdin, stdout, stderr

            if(firstCommand == "setenv") {
                processSetenv(extractedCommand, clientSockFd);
                break;
            } else if(firstCommand == "printenv") {
                processGetenv(extractedCommand, clientSockFd);
                break;
            } else if(firstCommand == "who") {
                processWho(clientSockFd);
                break;
            } else if(firstCommand == "name") {
                processName(extractedCommand, clientSockFd);
                break;
            } else if(firstCommand == "yell") {
                processYell(parsedCommand, clientSockFd);
                break;
            } else if(firstCommand == "tell") {
                processTell(parsedCommand, clientSockFd);
                break;
            } else if(firstCommand == "exit") {
                // call return to processRequest for further cleanup
                return;
            } else if(lastCommand == "|") {
                openPipe(lastCommand);
            } else if(lastCommand == ">") {
                string fileName = modifiedCommand[idx + 1];
                int fd = open(fileName.c_str(), O_CREAT | O_TRUNC | O_WRONLY, S_IRWXU | S_IRWXG);
                output = fd;
                idx++;
            } else if(regex_match(lastCommand, numberedPipe) || regex_match(lastCommand, errorPipe)) {
                // shouldn't wait for the commands, since they should be finished in the future, not now.
                shouldWait = false;

                // share instead of open a pipe if the pipe is already existed.
                if(pipeNotExist(lastCommand)){
                    openPipe(lastCommand);
                }
            } else if(regex_match(lastCommand, userPipeIn)) {
                int writerId = stoi(lastCommand.substr(1));
                int checker = checkUserPipeIn(clientSockFd, writerId);

                Client* reader = clientAddr;
                // printMailBox(reader -> id);

                if(checker == USER_NOTEXIST) {
                    string msg = getUserNotExistString(writerId);
                    write(clientSockFd, msg.c_str(), msg.size());
                    break;
                } else if(checker == PIPE_NOT_EXISTED) {
                    string msg = getPipeNotExistedString(clientSockFd, writerId);
                    write(clientSockFd, msg.c_str(), msg.size());
                    break;
                } else {
                    int writerId = stoi(lastCommand.substr(1));

                    Client* reader = clientAddr; 
                    Client* writer = getClientById(writerId);

                    broadcast(clientSockFd, PIPEIN, rawCommand, writerId);
                    unsetMailBox(writer -> id, reader -> id);
                }

                string Pipe = "|";
                openPipe(Pipe);

            } else if(regex_match(lastCommand, userPipeOut)) {
                int readerId = stoi(lastCommand.substr(1));
                // check whether the command is legal, if not, broadcast and break
                int checker = checkUserPipeOut(clientSockFd, readerId);
                
                if(checker == USER_NOTEXIST) {
                    string msg = getUserNotExistString(readerId);
                    write(clientSockFd, msg.c_str(), msg.size());
                    break;
                } else if(checker == PIPE_EXISTED) {
                    string msg = getPipeExistedString(clientSockFd, readerId);
                    write(clientSockFd, msg.c_str(), msg.size());
                    break;
                } else {
                    // checker == PIPEOUT_OK
                    int readerId = stoi(lastCommand.substr(1));

                    Client* writer = clientAddr;
                    Client* reader = getClientById(readerId);

                    broadcast(clientSockFd, PIPEOUT, rawCommand, readerId);

                    setMailBox(writer -> id, reader -> id);
                    // printMailBox(reader -> id);
                }

                // shouldn't wait for the command, the reader might not read the message in the near future
                shouldWait = false;
            }


            // note that the write end of pipe is closed here
            // debug only
            // cout << "Before IO redirection: " << input << ' ' << output << ' ' << errOutput << ' ' << clientSockFd << '\n';
            
            

            redirectIO(input, output, errOutput, lastCommand, clientSockFd);

            // debug only
            // cout << "After IO redirection: " << input << ' ' << output << ' ' << errOutput << ' ' << clientSockFd << '\n';

            //debug only
            // checkIO(input, output, errOutput);


            char** args = getArgs(extractedCommand);
            // execution block
            int pid;

            // block the entire process if the parent can't fork anymore
            while((pid = fork()) < 0) {

                // waitpid(0, nullptr, 0): means wait for any child process that is created by the current process
                // reference: https://linux.die.net/man/2/waitpid, in the description part
                waitpid(0, nullptr, 0);
            }

            if(pid == 0) {
                processCommand(args, input, output, errOutput, lastCommand, clientSockFd);
            } else if(pid > 0){
                if(input != STDIN_FILENO)
                    close(input);
                cpids.push_back(pid);
            } else {
                cerr << "failed to fork a child";
            }

            

            // the numbered pipe is in the middle
            if(idx != modifiedCommand.size() - 1 && (regex_match(lastCommand, numberedPipe) || regex_match(lastCommand, errorPipe))) {
                tick(false, true);

                // reset the shouldWait to true, since the numbered pipe is in the middle not the end
                shouldWait = true;
            }

            cleanUpPipe();
            // tick the countdown
            tick(true, false);
            idx++;
        }

        // we should wait only if the command is not piping the data to the future 
        if(shouldWait) {
            for(auto& cpid: cpids) {
                waitpid(cpid, nullptr, 0);
            }
        }

        
        tick(true, true);

        write(clientSockFd, "% ", strlen("% "));
    }
}

// reference: https://stackoverflow.com/questions/12774207/fastest-way-to-check-if-a-file-exists-using-standard-c-c11-14-17-c
bool canOpen(string command) {
    // no need to check built-in command
    if(command == "exit" || command == "printenv" || command == "setenv")
        return true;

    vector<string> commandPaths = getCommandPaths(command);

    for(auto& path: commandPaths) {
        if (FILE *file = fopen(path.c_str(), "r")) {
            fclose(file);
            return true;
        } 
    }
    return false; 
}

bool checkCommand(string command) {
    if(!canOpen(command)) {
        cerr << "Unknown command: [" << command << "]." << endl;
        return false;
    }
    return true;
}


// debug only
void checkIO(const int input, const int output, const int errOutput) {
    cout << "After redirect IO:\n input: " << input << "\noutput: " << output << "\nerrOut: " << errOutput << endl;
}

bool pipeNotExist(string& lastCommand) {
    int processTime = stoi(lastCommand.substr(1));
    for(auto& p: timer) {
        if(processTime == p.countdown)
            return false;
    }
    return true;
}


// decrease countdown value
// todo: delete Pipe instance when both end is closed
void tick(bool tickPipe, bool tickNumberedPipe) {
    for(auto& p: timer) {
        if(tickPipe && p.Type == "|") {
            p.countdown--;
        } 
        if(tickNumberedPipe && (regex_match(p.Type, numberedPipe) || regex_match(p.Type, errorPipe))) {
            p.countdown--;
        }
    }
}

vector<string> getCommandPaths(const string& commandName) {
    string rawPaths = getenv("PATH");
    rawPaths.push_back(':');

    vector<string> commandPaths;
    string path;

    for(auto& c: rawPaths) {
        if(c == ':') {
            commandPaths.push_back(path + "/" + commandName);
            path = "";
        } else {
            path.push_back(c);
        }
    }

    return commandPaths;
}

char** getArgs(const vector<string>& extractedCommand) {
    char** args = new char*[extractedCommand.size() + 1]; // an extra index for null terminator

    for(int i = 0; i < extractedCommand.size(); i++) {
        args[i] = new char[extractedCommand[i].size() + 1];
        strcpy(args[i], extractedCommand[i].c_str());
    }

    args[extractedCommand.size()] = nullptr;

    return args;
}

void redirectIO(int& input, int& output, int& errOutput, string& lastCommand, int clientSockFd) {
    // set io to default, the default output, errOutput is clientSockFd now
    // TODO: make sure the output from '>' works fine
    input = STDIN_FILENO;
    errOutput = clientSockFd;

    // if output != STDIN, then the output is modified by '>'
    if(output == STDOUT_FILENO)
        output = clientSockFd;

    // other commands are piping data in
    for(auto& pipe: timer) {
        if(pipe.countdown == 0) {
            input = pipe.readEnd;
            // cout << "The client close the fd in redirectIO: " << pipe.writeEnd << '\n';
            close(pipe.writeEnd);
        }
    }

    if(lastCommand == "|") {
        for(auto& pipe: timer) {
            // the command which the current command is going to pipe to. 
            if(pipe.countdown == 1) {
                output = pipe.writeEnd;
                // the read end should remain open, since the future command still need it.
            }
        }
    } else if(regex_match(lastCommand, numberedPipe) || regex_match(lastCommand, errorPipe)) {
        int processTime = stoi(lastCommand.substr(1));
        for(auto& pipe: timer) {
            if(processTime == pipe.countdown) {
                output = pipe.writeEnd;
                if(regex_match(lastCommand, errorPipe)) {
                    errOutput = pipe.writeEnd;
                }
            }
        }
    } else if(regex_match(lastCommand, userPipeOut)) {
        // noop
    } else if(regex_match(lastCommand, userPipeIn)) {
        for(auto& pipe: timer) {
            // the command which the current command is going to pipe to. 
            if(pipe.countdown == 1) {
                output = pipe.writeEnd;
                // the read end should remain open, since the future command still need it.
            }
        }
    }
}

void processCommand(char** args, int& input, int& output, int& errOutput, string& lastCommand, int clientSockFd) {
    // need to modify and close the io fd if the io is not in its default settings, such as input != STDIN


    // clientSockFd become the default Output fd here
    dup2(clientSockFd, STDOUT_FILENO);
    dup2(clientSockFd, STDERR_FILENO);

    
    close(clientSockFd);


    if(input != STDIN_FILENO){
        if(dup2(input, STDIN_FILENO) == -1) {
            cout << "bad fd: " << input << endl;
            perror("stdin, dup2 failed");
        }
        close(input);
    }
    if(output != clientSockFd) {
        if(dup2(output, STDOUT_FILENO) == -1) {
            perror("stdout, dup2 failed");
        }

        // IMPORTANT: we can only close the output here if output != errOutput, otherwise errOutput is a bad fd
        if(output != errOutput || errOutput == STDERR_FILENO)
            close(output);
    }
    if(errOutput != clientSockFd) {
        if(dup2(errOutput, STDERR_FILENO) == -1) {
            perror("stderr, dup2 failed");
        }
        close(errOutput);
    } 

    bool ok = checkCommand((string)args[0]);
    if(!ok)
        exit(1);

    if(regex_match(lastCommand, userPipeOut)) {
        int readerId = stoi(lastCommand.substr(1));
        Client* writer = clientAddr;
        Client* reader = getClientById(readerId);

        string fileName = fifoName(writer -> id, reader -> id);

        int outputFd = open(fileName.c_str(), O_WRONLY);
        dup2(outputFd, STDOUT_FILENO);
    }

    if(regex_match(lastCommand, userPipeIn)) {
        int writerId = stoi(lastCommand.substr(1));
        Client* writer = getClientById(writerId);
        Client* reader = clientAddr;

        string fileName = fifoName(writer -> id, reader -> id);
        int inputFd = open(fileName.c_str(), O_RDONLY);
        dup2(inputFd, STDIN_FILENO);
    }

    vector<string> commandPaths = getCommandPaths(args[0]);
    for(auto& p: commandPaths) {
        if(execv(p.c_str(), args) == -1)
            continue;
    }
}

void openPipe(string& lastCommand) {
    int fd[2];
    pipe(fd);
    
    // debug only
    // cout << "open a pipe with readEnd: " << fd[0] << ", writeEnd: " << fd[1] << endl;

    Pipe p;
    p.readEnd = fd[0];
    p.writeEnd = fd[1];
    p.Type = lastCommand;

    if(lastCommand == "|") {
        p.countdown = 1;
    }
    else if(regex_match(lastCommand, numberedPipe) || regex_match(lastCommand, errorPipe)) {
        p.countdown = stoi(lastCommand.substr(1));
    } 

    timer.push_back(p);
}


// setenv [var] [value]
void processSetenv(const vector<string>& extractedCommand, int clientSockFd){
    // assume the var and value are both provided
    if(extractedCommand.size() != 3)
        return;

    string var = extractedCommand[1];
    string value = extractedCommand[2];

    if(setenv(var.c_str(), value.c_str(), 1) == -1){
        string err = "built-in commands error\n";
        // cerr << "built-in commands error" << endl;
        write(clientSockFd, err.c_str(), err.size());
    }
}

// getenv [var]
void processGetenv(const vector<string>& extractedCommand, int clientSockFd) {
    if(extractedCommand.size() != 2)
        return;

    string var = extractedCommand[1];


    char* resultPtr = getenv(var.c_str());
    if(resultPtr != nullptr) {
        // cout << resultPtr << endl;
        write(clientSockFd, resultPtr, strlen(resultPtr));


        // somehow the getenv didn't add a '\n' automatically at the end of line
        string newLine = "\n";
        write(clientSockFd, newLine.c_str(), newLine.size());
    }
}


// read the parsedcommand until meets a special character(|, !, etc)
vector<string> extractCommand(const vector<string>& parsedCommand, int& idx, int& input, int& output, int& errOutput) {
    // default i/o fd
    input = STDIN_FILENO;
    output = STDOUT_FILENO;
    errOutput = STDERR_FILENO;


    // command is of the form [command name, arg1, arg2, ...]
    vector<string> extractedCommand;


    while(idx < parsedCommand.size()) {
        string curEntry = parsedCommand[idx];  
        extractedCommand.push_back(curEntry);

        if(curEntry == "|") {
            // consider as a special case of numbered pipe
            break;
        } else if(curEntry == ">") {
            break;
        } else if(regex_match(curEntry, numberedPipe) || regex_match(curEntry, errorPipe)) {
            // processTimeStamp is the timeStamp of the read end
            break;
        } else if(regex_match(curEntry, userPipeIn) || regex_match(curEntry, userPipeOut)) {
            break;
        }
        idx++;
    }

    return extractedCommand;
}

// extract every single command, separated by space
vector<string> parseCommand(string rawCommand) {
    vector<string> parsedCommand;
    string entry; 
    rawCommand += " ";

    for(auto& c: rawCommand) {
        if(c == ' ' || c == '\r' || c == '\n') {
            if(entry.size() > 0) {
                parsedCommand.push_back(entry);
            }
            entry = "";
        } else {
            entry.push_back(c);
        }
    }

    return parsedCommand;
}

// init the socket, doing the jobs include, socket(), bind() and listen(), return the socketFd
int initSocket(int portNumber) {
    int serverSockFd = socket(AF_INET , SOCK_STREAM , 0); //socket(ipv4, tcp, protocol)
    if(serverSockFd == -1) {
        cerr << "Fail to create socket: " << errno << endl;
        exit(-1);
    }

    // follow the C style here
    struct sockaddr_in serverSockAddrIn;

    // inplace initialization of serverSockAddrIn
    initSockAddrIn(serverSockAddrIn, portNumber);

    // int one = 1;
    // setsockopt(serverSockFd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(int));

    // bind the server to the socket
    // Why sockaddr*: https://stackoverflow.com/questions/18609397/whats-the-difference-between-sockaddr-sockaddr-in-and-sockaddr-in6
    if(bind(serverSockFd, (struct sockaddr*)(&serverSockAddrIn), sizeof(serverSockAddrIn)) == -1) {
        cerr << "Fail to bind: " << errno << endl;
        exit(-1); 
    }


    // backlog = 0, no pending
    if(listen(serverSockFd, MAX_CLIENT) == -1) {
        cerr << "Fail to listen: " << errno << endl;
        exit(-1);
    } 


    return serverSockFd;
}

// initialize the serverSockAddrIn inplace
void initSockAddrIn(struct sockaddr_in& serverSockAddrIn, int portNumber) {
    bzero(&serverSockAddrIn, sizeof(serverSockAddrIn));     // set all the data in serverSockAddrIn to 0
    serverSockAddrIn.sin_family = PF_INET;                  // IPv4
    serverSockAddrIn.sin_addr.s_addr = INADDR_ANY;          // 0.0.0.0
    serverSockAddrIn.sin_port = htons(portNumber);          // port, htons(portNumber) convert portNumber to the correct endianess
}



/*     
    Ref: https://www.geeksforgeeks.org/ipc-shared-memory/

    ftok(): is use to generate a unique key.

    shmget(): int shmget(key_t,size_tsize,intshmflg); upon successful completion, shmget() returns an identifier for the shared memory segment.

    shmat(): Before you can use a shared memory segment, you have to attach yourself
    to it using shmat(). void *shmat(int shmid ,void *shmaddr ,int shmflg);
    shmid is shared memory id. shmaddr specifies specific address to use but we should set
    it to zero and OS will automatically choose the address.

    shmdt(): When you’re done with the shared memory segment, your program should
    detach itself from it using shmdt(). int shmdt(void *shmaddr);

    shmctl(): when you detach from shared memory,it is not destroyed. So, to destroy
    shmctl() is used. shmctl(int shmid,IPC_RMID,NULL);
*/
void processRequest(int portNumber) {
    int serverSockFd = initSocket(portNumber); // socket, bind, listen


    signal(SIGUSR1, broadcasting);

    cout << "initializing share memory...\n";
    initShareMem();
    cout << "initilized\n";

    // https://stackoverflow.com/questions/6168636/how-to-trigger-sigusr1-and-sigusr2
    // register broadcasting to the sigusr1 signal, and use kill to run the broadcasting
    // and since kill is using the api kill(pid, SIGUSR1), the pid need to be stored in the Client struct
    // signal(SIGUSR1, broadcasting);

    while(true) {

        cout << "waiting for connection...\n";
        struct sockaddr_in clientSockAddrIn;
        socklen_t addrlen = sizeof(clientSockAddrIn);
        int clientSockFd = accept(serverSockFd, (struct sockaddr*)(&clientSockAddrIn), &addrlen);

        if(clientSockFd == -1) {
            cout << "here";
            cerr << "Fail to accept client: " << errno << endl;
            exit(-1);
        }

        cout << "connected\n";


        cout << "forking...\n";
        int pid = fork();
        if(pid < 0) {
            cerr << "Fail to fork a child for client" << endl;
            exit(-1);
        } else if(pid == 0) {
            close(serverSockFd);

            // the std i o err will not be used anymore
            dup2(clientSockFd, STDIN_FILENO);
            dup2(clientSockFd, STDOUT_FILENO);
            dup2(clientSockFd, STDERR_FILENO);


            
            // the path env need no switching back and forth anymore
            setenv("PATH", "bin:.", 1);


            initClient(clientSockFd);

            printWelcome(clientSockFd);

            clientAddr -> address = clientSockAddrIn.sin_addr;
            clientAddr -> port = ntohs(clientSockAddrIn.sin_port);
            clientAddr -> pid = getpid();

            broadcast(clientSockFd, LOGIN);


            npshell(clientSockFd);

            broadcast(clientSockFd, LOGOUT);

            cleanUpClient();
            close(clientSockFd);
        } else {
            // the clientSockFd is used by client not parent
            close(clientSockFd);
            cout << "forked\n";
        }
    }
}

void initShareMem() {
    key_t key;

    // one block 0, pub the shm.cpp under the user_pipe folder, otherwise creating this file will cause the ls gives different output
    ofstream f ("./user_pipe/shm.cpp");
    f.close();

    key = ftok("./user_pipe/shm.cpp", 0);
    size_t size = sizeof(Client) * MAX_CLIENT;

    // ipcrm -m id to remove memory
    int blockId = shmget(key, size, 0644 | IPC_CREAT);
    if(blockId == -1) {
        cout << errno << '\n';
        cout << "terminated with block id: " << blockId << endl;
        exit(1);
    }

    startAddr = (Client*)shmat(blockId, NULL, 0);
    if(startAddr == nullptr) {
        cout << "startAddr Error";
        exit(1);
    }

    initClientMem();
}

void initClientMem() {
    for(int i = 0; i < MAX_CLIENT; i++) {
        Client* ptr = (startAddr + i);
        // the field is NULL indicate the block is free to use, empty.
        ptr -> id = NULL_ID;
        ptr -> clientSockFd = NULL_FD;
    }
}

void broadcast(int clientSockFd, int broadcastType, string info1, int id) {
    Client client = *clientAddr;
    string msg;
    if(broadcastType == LOGIN) {
        msg = getLoginString(clientSockFd);
    } else if(broadcastType == LOGOUT) {
        msg = getLogOutString(clientSockFd);
    } else if(broadcastType == NAME) {
        msg = getNameString(clientSockFd);
    } else if(broadcastType == YELL) {
        msg = getYellString(clientSockFd, info1);
    } else if(broadcastType == TELL) {
        msg = getTellString(clientSockFd, info1);
    }else if(broadcastType == PIPEIN) {
        msg = getPipeInString(clientSockFd, id, info1);
    } else if(broadcastType == PIPEOUT) {
        msg = getPipeOutString(clientSockFd, id, info1);
    } 

    // broadcast to everyone if the type is not tell
    if(broadcastType != TELL) {
        for(int i = 0; i < MAX_CLIENT; i++) {
            Client* ptr = (startAddr + i);
            if(ptr -> id != NULL_ID) {
                // cannot convert ‘std::string’ {aka ‘std::basic_string<char>’} to ‘const char*’
                strcpy(ptr -> news, msg.c_str());
                // send the signal
                kill(ptr -> pid, SIGUSR1);
            }
        }
    } else {
        for(int i = 0; i < MAX_CLIENT; i++) {
            Client* ptr = (startAddr + i);
            // send the message to only the reciever
            if(ptr -> id != NULL_ID && ptr -> id == id) {
                strcpy(ptr -> news, msg.c_str());
                kill(ptr -> pid, SIGUSR1);
                break;
            }
        }
    }
}


void initClient(int clientSockFd) {
    for(int i = 0; i < MAX_CLIENT; i++) {
        Client* ptr = (startAddr + i);
        if(ptr -> id == NULL_ID && ptr -> clientSockFd == NULL_FD) {
            ptr -> id = i + 1;
            ptr -> clientSockFd = clientSockFd;
            strcpy(ptr -> name, "(no name)");
            strcpy(ptr -> news, "");
            // cout << "finished write to slot...\n"
            for(int j = 0; j <= MAX_CLIENT; j++)
                ptr -> mailBox[j] = 0;
            clientAddr = ptr;
            return;
        }
    }
}

void printWelcome(int clientSockFd) {
    string msg = "****************************************\n** Welcome to the information server. **\n****************************************\n";

    write(clientSockFd, msg.c_str(), msg.size());   
}


string getLoginString(int clientSockFd) {
    Client* client = clientAddr;
    string msg = "*** User '" + string(client -> name) + "' entered from " + inet_ntoa(client -> address) + ":" + to_string(client -> port) + ". ***\n";
    // write(clientSockFd, msg.c_str(), msg.size());
    return msg;
}

string getLogOutString(int clientSockFd) {
    Client* client = clientAddr;
    string msg = "*** User '" + string(client -> name) + "' left. ***\n";
    return msg;
}

string getNameString(int clientSockFd) {
    Client* client = clientAddr;

    // weird datatype bug
    string msg = "*** User from ";
    msg += inet_ntoa(client -> address);
    msg += ":" + to_string(client -> port) + " is named '" + client -> name + "'. ***\n";
    return msg;
}

// the processYell receivet the entire parsedCommand, since every thing after the user id is the message
void processYell(const vector<string>& parsedCommand, int clientSockFd) {
    if(parsedCommand.size() == 1)
        return;

    string msg;
    for(int i = 1; i < parsedCommand.size(); i++) {
        msg += parsedCommand[i];
        if(i != parsedCommand.size() - 1)
            msg += " ";
    }
    
    broadcast(clientSockFd, YELL, msg);
}

string getYellString(int clientSockFd, string yellString) {
    Client* client = clientAddr;
    string msg = "*** " + string(client -> name) + " yelled ***: " + yellString + "\n";
    return msg;
}


string getPipeInString(int clientSockFd, int writerId, string command) {
    Client* reader = clientAddr;
    Client* writer = getClientById(writerId);
    
    string msg = "*** ";
    msg += string(reader -> name) + " (#" + to_string(reader -> id) + ") just received from " + writer -> name + " (#" + to_string(writer -> id) + ") by '" + command + "'***\n";
    return msg;
}

string getPipeOutString(int clientSockFd, int readerId, string command) {
    Client* reader = getClientById(readerId);
    Client* writer = clientAddr;
    string name1 = writer -> name;
    string name2 = reader -> name;

    string msg = "*** ";
    
    msg += (name1) + " (#" + to_string(writer -> id) + ") just piped '" + command + "' to "+ (name2) + " (#" + to_string(reader -> id) + ") ***\n";
    return msg;
}

Client* getClientById(int id) {
    for(int i = 0; i < MAX_CLIENT; i++) {
        Client* ptr = (startAddr + i);
        if(ptr -> id == id) {
            return ptr;
        }
    }

    return nullptr;
}

void broadcasting(int s) {
    string news = string(clientAddr -> news);
    write(clientAddr -> clientSockFd, news.c_str(), news.size());
    // cout << news << '\n';
    // reset the news
    strcpy(clientAddr -> news, "");
}


void processName(const vector<string>& extractedCommand, int clientSockFd) {
    if(extractedCommand.size() != 2) {
        return;
    }

    Client* client = clientAddr;
    string name = extractedCommand[1];
    for(int i = 0; i < MAX_CLIENT; i++) {
        Client* ptr = (startAddr + i);
        if(name == ptr -> name) {
            string msg = "*** User '" + name + "' already exists. ***\n";
            write(clientSockFd, msg.c_str(), msg.size());
            return;
        }
    }

    strcpy(client -> name, name.c_str());

    broadcast(clientSockFd, NAME);
}

void processWho(int clientSockFd) {
    string header = "<ID>\t<nickname>\t<IP:port>\t<indicate me>\n";
    write(clientSockFd, header.c_str(), header.size());

    int curId = clientAddr -> id;
    string msg;    

    for(int i = 0; i < MAX_CLIENT; i++) {
        Client* ptr = (startAddr + i);
        if(ptr -> id != NULL_ID) {
            msg += to_string(ptr -> id) + '\t' + ptr -> name + '\t' + inet_ntoa(ptr -> address) + ":" + to_string(ptr -> port);
            if(ptr -> id == curId) {
                msg += "\t<-me";
            }
            msg += '\n';
        }
    }
    write(clientSockFd, msg.c_str(), msg.size());
}



void processTell(const vector<string>& parsedCommand, int clientSockFd) {
    if(parsedCommand.size() <= 2)
        return;
    
    int readerId = stoi(parsedCommand[1]);

    Client* writer = clientAddr;
    Client* reader = getClientById(readerId);
    if(reader == nullptr) {
        string msg = "*** Error: user #" + to_string(readerId) +" does not exist yet. ***\n";
        write(clientSockFd, msg.c_str(), msg.size());
        return;
    } else {
        string msg;
        for(int i = 2; i < parsedCommand.size(); i++) {
            msg += parsedCommand[i];
            if(i != parsedCommand.size() - 1)
                msg += " ";
        }
        broadcast(clientSockFd, TELL, msg, readerId);
    }
}

string getTellString(int clientSockFd, string tellString) {
    Client* client = clientAddr;
    string msg = "*** " + string(client -> name) + " told you ***: " + tellString + "\n";
    return msg;
}

void cleanUpClient() {
    // Todo: close every 

    // cleanUp others mailbox[clientId]
    for(int i = 0; i <= MAX_CLIENT; i++) {
        Client* ptr = (startAddr + i);
        if(ptr == clientAddr)
            continue;

        if(ptr -> mailBox[clientAddr -> id] == 1) {
            string fileName = fifoName(clientAddr -> id, ptr -> id);

            // read out the datafrom the pipe
            // cout << "cleaning mailbox...\n";
            open(fileName.c_str(), O_RDONLY | O_NONBLOCK);

            ptr -> mailBox[clientAddr -> id] = 0;
        }

        if(clientAddr -> mailBox[ptr -> id] == 1) {
            string fileName = fifoName(ptr -> id, clientAddr -> id);

            // cout << "cleaning mailbox...\n";
            open(fileName.c_str(), O_RDONLY | O_NONBLOCK);
            // cout << "cleaned\n";
            clientAddr -> mailBox[ptr -> id] = 0;
        }
    }

    memset(clientAddr, 0, sizeof(Client));
    clientAddr -> id = NULL_ID;
    clientAddr -> clientSockFd = NULL_FD;

    // detach process from the shm
    // shmdt(startAddr);
}


vector<string> modifyCommand(vector<string> parsedCommand) {
    vector<string> modifiedCommand;

    for(int i = 0; i < parsedCommand.size(); i++) {
        if(i != parsedCommand.size() - 1 && regex_match(parsedCommand[i], userPipeOut) && regex_match(parsedCommand[i + 1], userPipeIn)) {
            swap(parsedCommand[i], parsedCommand[i + 1]);
        }
    }

    for(int i = 0; i < parsedCommand.size(); i++) {
        if(!regex_match(parsedCommand[i], userPipeIn)) {
            modifiedCommand.push_back(parsedCommand[i]);
        } else {
            modifiedCommand.push_back(parsedCommand[i]);
            modifiedCommand.push_back("cat");
        }
    }

    return modifiedCommand;
}


void cleanUpPipe() {
    vector<Pipe> newTimer;

    for(auto& p: timer) {
        if(p.countdown == 0)
            continue;
        newTimer.push_back(p);
    }

    timer = newTimer;
}

int checkUserPipeIn(int clientSockFd, int writerId) {
    Client* writer = getClientById(writerId);
    Client* reader = clientAddr;
    if(writer == nullptr) {
        return USER_NOTEXIST;
    }
    
    if(!isMailBoxOpen(writer -> id, reader -> id))
        return PIPE_NOT_EXISTED;

    return PIPEIN_OK;
}


int checkUserPipeOut(int writerFd, int clientId) {
    Client* reader = getClientById(clientId);
    Client* writer = clientAddr;
    if(reader == nullptr) {
        return USER_NOTEXIST;
    } 
    if(isMailBoxOpen(writer -> id, reader -> id))
        return PIPE_EXISTED;

    return PIPEOUT_OK;
}

bool isMailBoxOpen(int writerId, int readerId) {
    int mailBoxState = (startAddr + readerId - 1) -> mailBox[writerId];
    if(mailBoxState == 1)
        return true;
    return false;
}

string getUserNotExistString(int readerId) {
    string msg = "*** Error: user #" + to_string(readerId) + " does not exist yet. ***\n";
    return msg;
}


string getPipeNotExistedString(int clientSockFd, int writerId) {
    int readerId = clientAddr -> id;

    string msg = "*** Error: the pipe #" + to_string(writerId) + "->#" + to_string(readerId) + " does not exist yet. ***\n";
    return msg;
}   

string getPipeExistedString(int clientSockFd, int readerId) {
    int writerId = clientAddr -> id;

    string msg = "*** Error: the pipe #" + to_string(writerId) + "->#" + to_string(readerId) + " already exists. ***\n";
    return msg;
}

string fifoName(int writerId, int readerId) {
    string pathName = "./user_pipe/" + to_string(writerId) + "to" + to_string(readerId);
    return pathName;
}

void initMkfifo() {
    for(int i = 1; i <= MAX_CLIENT; i++) {
        for(int j = 1; j <= MAX_CLIENT; j++) {
            if(i != j) {
                string pathName = fifoName(i, j);
                mkfifo(pathName.c_str(), 0777);
            }
        }
    }
}

void setMailBox(int writerId, int readerId) {
    Client* ptr = (startAddr + readerId - 1);
    (ptr -> mailBox)[writerId] = 1;
}

void unsetMailBox(int writerId, int readerId) {
    Client* ptr = (startAddr + readerId - 1);
    ptr -> mailBox[writerId] = 0;
}

void printMailBox(int id) {
    cout << "printing the mailbox of reader: " << id << '\n';

    Client* ptr = startAddr;
    for(int i = 0; i < MAX_CLIENT; i++) {
        cout << (ptr + id - 1) -> mailBox[i] << ' ';
    }
}

void cleaning(int s) {
    shmdt(startAddr);
    exit(1);
}