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

#define BUFFERSIZE          30000
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

using namespace std;

#define ll long long

// note: 關於error message與%的先後順序https://e3.nycu.edu.tw/mod/dcpcforum/discuss.php?d=206417
ll timeStamp = 0;   


// timeStamp -> [[fd[0], fd[1]], [fd[0], fd[1]]]
struct Pipe {
    int readEnd;
    int writeEnd;

    // type is a reserved word
    string Type; 
    int countdown;

    // Fd of writer and reader
    int writerFd; 
    int readerFd;
};

struct UserPipe {
    // Decided by the pipe api
    int readEnd;    
    int writeEnd;

    // the fd of clients
    int writerFd;
    int readerFd;
};

fd_set validFds;
fd_set readFds;
fd_set newReadFds;

vector<int> idPool;


struct Client {
    int id;
    string name;
    int clientSockFd;
    map<string, string> envVars;
    in_addr address;
    u_short port;
    bool invalid;
};

vector<Pipe> timer;
vector<UserPipe> userPipes;

vector<Client*> clients;

// use regex to match the numbered pipe operator
regex numberedPipe("\\|[0-9]+");
regex errorPipe("\\![0-9]+");
regex userPipeOut(">[0-9]+");
regex userPipeIn("<[0-9]+");

int npshell(vector<string>, int);
vector<string> parseCommand(string);
vector<string> extractCommand(const vector<string>&, int&, int&, int&, int&);
void processGetenv(const vector<string>&, int);
void processSetenv(const vector<string>&, int);
void openPipe(string Type, int);
void processCommand(char**, int&, int&, int&, string&, int);
void redirectIO(int&, int&, int&, string&, int);
char** getArgs(const vector<string>&);
vector<string> getCommandPaths(const string&);
void tick(bool, bool, int);
bool pipeNotExistByTime(string&);
void checkIO(const int, const int, const int);
bool checkCommand(string);
bool canOpen(string);
int initSocket(int);
void initSockAddrIn(struct sockaddr_in&, int);
void processRequest(int);
int initFdTable(int);
void initClient(int, struct sockaddr_in);
void printWelcome(int);
string getLoginString(int);
string getLogoutString(int);
void printPrompt(int);
void cleanUpClient(int);
void restoreClientEnv(int);
void broadcast(int, int, string="", int=-1);
int getIdFromPool();
void processWho(int);
void processTell(const vector<string>&, int);
void processYell(const vector<string>&, int);
void processName(const vector<string>&, int);
string getNameString(int);
string getYellString(int, string);
int checkUserPipeOut(int, int);
string getUserNotExistString(int);
string getPipeExistedString(int, int);
string getPipeNotExistedString(int, int);
int checkUserPipeIn(int, int);
string getPipeInString(int, int, string);
string getPipeOutString(int, int, string);
void openUserPipe(string&, int, int);
vector<string> modifyCommand(vector<string>);
void cleanUpUserPipe(int, int);
void cleanUpPipe(int);


// return a pointer, sometime we need to modify it.
Client* getClientByFd(int);
Client* getClientById(int);

// if registeredPipe{writerFd, readerFd} = True, then that pipe is used
map<pair<int, int>, bool> registeredPipe;


// TODO: number doesn't work correctly

// ./np_simple_proc port_number
int main(int argc, char* const argv[]) {
    if(argc != 2) {
        cerr << "You need to execute the process by: ./np_simple_proc port_number" << endl;
        return -1;
    }


    // set the initial PATH environment variable;
    setenv("PATH", "bin:.", 1);
    
    // init the socket here
    int portNumber = atoi(argv[1]);
    processRequest(portNumber);
    // npshell();
}


// npshell need to write to clientSockFd by calling write instead of cout now
int npshell(vector<string> parsedCommand, int clientSockFd) {
    // the child pids, only wait when the command is not a numbered pipe(since it need to be finished now).
    vector<int> cpids;

    // no input
    if(parsedCommand.size() == 0){
        printPrompt(clientSockFd);
        return CLIENT_STAY;
    }

    string rawCommand;
    for(int i = 0; i < parsedCommand.size(); i++) {
        rawCommand += parsedCommand[i];
        if(i != parsedCommand.size() - 1)
            rawCommand += ' ';
    }

    vector<string> modifiedCommand = modifyCommand(parsedCommand);

    cout << "modified command: ";
    for(auto& c: modifiedCommand) {
        cout << c << ' ';
    }
    cout << '\n';

    int idx = 0;
    bool shouldWait = true;

    while(idx < modifiedCommand.size()) {
        // IO fd, the output is send to the client
        int input = -1;
        int output = -1;
        int errOutput = -1;
        bool cleanUserPipes = false;

        vector<string> extractedCommand = extractCommand(modifiedCommand, idx, input, output, errOutput);
        string lastCommand = extractedCommand.back();
        string firstCommand = extractedCommand[0];

        cout << "extractedCommand: ";
        for(auto&i : extractedCommand) {
            cout << i << ' ';
        }
        cout << '\n';
        cout << "lastCommand: " << lastCommand << ", firstCommand: " << firstCommand << '\n';


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
            return CLIENT_EXIT;
        } else if(lastCommand == "|") {
            openPipe(lastCommand, clientSockFd);
        } else if(lastCommand == ">") {
            string fileName = modifiedCommand[idx + 1];
            int fd = open(fileName.c_str(), O_CREAT | O_TRUNC | O_WRONLY, S_IRWXU | S_IRWXG);
            output = fd;
            idx++;
        } else if(regex_match(lastCommand, numberedPipe) || regex_match(lastCommand, errorPipe)) {
            // shouldn't wait for the commands, since they should be finished in the future, not now.
            shouldWait = false;

            // share instead of open a pipe if the pipe is already existed.
            if(pipeNotExistByTime(lastCommand)){
                openPipe(lastCommand, clientSockFd);
            }
        } else if(regex_match(lastCommand, userPipeIn)) {
            int writerId = stoi(lastCommand.substr(1));
            int checker = checkUserPipeIn(clientSockFd, writerId);

            if(checker == USER_NOTEXIST) {
                string msg = getUserNotExistString(writerId);
                write(clientSockFd, msg.c_str(), msg.size());
                break;
            } else if(checker == PIPE_NOT_EXISTED) {
                string msg = getPipeNotExistedString(clientSockFd, writerId);
                write(clientSockFd, msg.c_str(), msg.size());
                break;
            } else {
                broadcast(clientSockFd, PIPEIN, rawCommand, writerId);
                int writerFd = getClientById(writerId) -> clientSockFd;
                registeredPipe[{writerFd, clientSockFd}] = false;
                cleanUserPipes = true;
            }

            openPipe("|", clientSockFd);
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
                broadcast(clientSockFd, PIPEOUT, rawCommand, readerId);
                int readerFd = getClientById(readerId) -> clientSockFd;
                registeredPipe[{clientSockFd, readerFd}] = true;
            }

            // shouldn't wait for the command, the reader might not read the message in the near future
            shouldWait = false;
            openUserPipe(lastCommand, clientSockFd, readerId);
        }

        // note that the write end of pipe is closed here
        // debug only
        cout << "\nBefore IO redirection: " << input << ' ' << output << ' ' << errOutput << ' ' << clientSockFd << '\n';
        
        cout << fcntl(8, F_GETFD) << '\n';
        redirectIO(input, output, errOutput, lastCommand, clientSockFd);
        cout << fcntl(8, F_GETFD) << '\n';

        // debug only
        cout << "After IO redirection: " << input << ' ' << output << ' ' << errOutput << ' ' << clientSockFd << "\n\n";

        //debug only
        // checkIO(input, output, errOutput);

        // the <number command need to pipe the data to the next command
        // note that this line need to be executed after redirectIO, otherwise the pipe data will be fetched immediately
        


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
            if(input != STDIN_FILENO){
                cout << "parent close write end: " << input << '\n';  
                close(input);
            }
            cpids.push_back(pid);
        } else {
            cerr << "failed to fork a child";
        }

    
        if(cleanUserPipes) {
            int writerId = stoi(lastCommand.substr(1));
            cleanUpUserPipe(clientSockFd, writerId);
        }

        // the numbered pipe is in the middle
        if(idx != modifiedCommand.size() - 1 && (regex_match(lastCommand, numberedPipe) || regex_match(lastCommand, errorPipe))) {
            tick(false, true, clientSockFd);

            // reset the shouldWait to true, since the numbered pipe is in the middle not the end
            shouldWait = true;
        }

        cleanUpPipe(clientSockFd);

        // tick the countdown
        tick(true, false, clientSockFd);
        idx++;
    }


    // we should wait only if the command is not piping the data to the future 
    if(shouldWait) {
        for(auto& cpid: cpids) {
            waitpid(cpid, nullptr, 0);
        }
    }

    tick(true, true, clientSockFd);

    printPrompt(clientSockFd);
    return CLIENT_STAY;
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

bool pipeNotExistByTime(string& lastCommand) {
    int processTime = stoi(lastCommand.substr(1));
    for(auto& p: timer) {
        if(processTime == p.countdown)
            return false;
    }
    return true;
}

// decrease countdown value
// todo: delete Pipe instance when both end is closed
// DO NOT tick other's timer
void tick(bool tickPipe, bool tickNumberedPipe, int clientSockFd) {
    for(auto& p: timer) {
        // tick only if the writer is that clientSockFd
        if(tickPipe && p.Type == "|" && p.writerFd == clientSockFd) {
            p.countdown--;
        } 

        // tick only if the writer is that clientSockFd
        if(tickNumberedPipe && (regex_match(p.Type, numberedPipe) || regex_match(p.Type, errorPipe)) && p.writerFd == clientSockFd) {
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
    input = STDIN_FILENO;
    errOutput = clientSockFd;

    // if output != STDIN, then the output is modified by '>'
    if(output == STDOUT_FILENO)
        output = clientSockFd;

    // other commands are piping data in
    for(auto& pipe: timer) {

        // read the pipe only if countdown == 0 and the pipe is for that client
        if(pipe.countdown == 0 && pipe.readerFd == clientSockFd) {
            input = pipe.readEnd;
            cout << "The client close the pipe: " << pipe.writeEnd << '\n';
            close(pipe.writeEnd);
        }
    }

    

    // for(auto& pipe: userPipes) {
    //     if(pipe.readerFd == clientSockFd && )
    // }

    if(lastCommand == "|") {
        for(auto& pipe: timer) {
            // the command which the current command is going to pipe to. 
            if(pipe.countdown == 1 && pipe.writerFd == clientSockFd) {
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

        int readerId = stoi(lastCommand.substr(1));
        Client* reader = getClientById(readerId);

        for(auto& p: userPipes) {
            if(p.readerFd == reader -> clientSockFd && p.writerFd == clientSockFd) {
                output = p.writeEnd;
            }
        }
    } else if(regex_match(lastCommand, userPipeIn)) {
        int writerId = stoi(lastCommand.substr(1));
        Client* writer = getClientById(writerId);

        for(auto& p: userPipes) {
            if(p.readerFd == clientSockFd && p.writerFd == writer -> clientSockFd) {
                input = p.readEnd;
                close(p.writeEnd);
            }
        }
        
        for(auto& pipe: timer) {
            // the command which the current command is going to pipe to. 
            if(pipe.countdown == 1 && pipe.writerFd == clientSockFd) {
                cout << "redirect the output from " << output << " to " << pipe.writeEnd << '\n';
                output = pipe.writeEnd;
                // the read end should remain open, since the future command still need it.
            }
        }
    }
}

void processCommand(char** args, int& input, int& output, int& errOutput, string& lastCommand, int clientSockFd) {
    // need to modify and close the io fd if the io is not in its default settings, such as input != STDIN
    // cout << input << ' ' << output << ' ' << errOutput << ' ' << clientSockFd << '\n';

    // clientSockFd become the default Output fd here
    cout << "IO at processCommand: " << input << " " << output << " " << errOutput << " " << clientSockFd << '\n';

    dup2(clientSockFd, STDOUT_FILENO);
    dup2(clientSockFd, STDERR_FILENO);
    close(clientSockFd);


    if(input != STDIN_FILENO){
        if(dup2(input, STDIN_FILENO) == -1) {
            perror("stdin dup2 failed");
        }
        // cout << "child " << clientSockFd << "close input: " << input << '\n';
        close(input);
    }
    if(output != clientSockFd) {
        if(dup2(output, STDOUT_FILENO) == -1) {
            cout << "Client: " << clientSockFd << "dup a bad fd: " << output << "\n";
            perror("stdout dup2 failed");
        }

        // IMPORTANT: we can only close the output here if output != errOutput, otherwise errOutput is a bad fd
        if(output != errOutput || errOutput == STDERR_FILENO) {
            close(output);
            // cout << "child " << clientSockFd <<  "close output: " << output << '\n';
        }
    }
    if(errOutput != clientSockFd) {
        if(dup2(errOutput, STDERR_FILENO) == -1) {
            perror("stderr dup2 failed");
        }
        close(errOutput);
    }

    bool ok = checkCommand((string)args[0]);
    if(!ok)
        exit(1);

    vector<string> commandPaths = getCommandPaths(args[0]);
    for(auto& p: commandPaths) {
        if(execv(p.c_str(), args) == -1)
            continue;
    }
}

void openUserPipe(string& lastCommand, int clientSockFd, int readerId) {
    Client* reader = getClientById(readerId);
    int fd[2];
    pipe(fd);

    UserPipe p;
    p.readEnd = fd[0];
    p.writeEnd = fd[1];

    p.writerFd = clientSockFd;
    p.readerFd = reader -> clientSockFd;

    cout << "Client open a user pipe: " << "write fd: " << fd[1] << ", read fd: " << fd[0] << '\n';

    userPipes.push_back(p);
}

void openPipe(string lastCommand, int clientSockFd) {
    int fd[2];
    pipe(fd);
    
    // debug only
    // cout << fd[0] << ' ' << fd[1] << endl;
    cout << "ClientSockFd: " << clientSockFd << " opened a pipe with readend: " << fd[0] << ", writeend: " << fd[1] << '\n';

    Pipe p;
    p.readEnd = fd[0];
    p.writeEnd = fd[1];
    p.Type = lastCommand;
    p.writerFd = clientSockFd;
    p.readerFd = clientSockFd;

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
        return;
    } 

    // not only the setenv need to be executed, the envVars in the Client struct need to be updated as well

    auto client = getClientByFd(clientSockFd);
    client -> envVars[var] = value;
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

    // auto client = getClientByFd(clientSockFd);
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

    int one = 1;
    setsockopt(serverSockFd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(int));

    // bind the server to the socket
    // Why sockaddr*: https://stackoverflow.com/questions/18609397/whats-the-difference-between-sockaddr-sockaddr-in-and-sockaddr-in6
    if(bind(serverSockFd, (struct sockaddr*)(&serverSockAddrIn), sizeof(serverSockAddrIn)) == -1) {
        cerr << "Fail to bind: " << errno << endl;
        exit(-1); 
    }


    // backlog = 0, no pending, backlog affects is how many incoming connections can queue up if your application isn't accept()
    if(listen(serverSockFd, 0) == -1) {
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

// use ip addr show to get my ip
void processRequest(int portNumber) {
    int serverSockFd = initSocket(portNumber); // socket, bind, listen

    // initialize fdTable, return validFds size and set validFds[serverSockFd] bit, and readFds[serverSockFd] bit
    int fdTableSize = initFdTable(serverSockFd);


    struct sockaddr_in clientSockAddrIn;
    socklen_t addrlen = sizeof(clientSockAddrIn);

    while(true) {
        
        /* 
                the server only read from the readFds, set the other field to null, "we always process the data is readFds[fd] is set"!!
                set the timeout to nullptr, so that the select block until one of the readFds is set.
                readfds


                The file descriptors in this set are watched to see if
                they are ready for reading.  A file descriptor is ready
                for reading if a read operation will not block; in
                particular, a file descriptor is also ready on end-of-
                file.

                After select() has returned, readfds will be cleared of
                all file descriptors except for those that are ready for
                reading.

                also: https://stackoverflow.com/questions/4563577/is-it-necessary-to-reset-the-fd-set-between-select-system-call
        
                need to reset the readFds everytime
        */

        newReadFds = readFds;
        select(fdTableSize, &readFds, nullptr, nullptr, nullptr);

        // always process the server itself, this check whether a client want to establish a connection 
        if(FD_ISSET(serverSockFd, &readFds)) {

            // init the client socket fd
            int clientSockFd = accept(serverSockFd, (struct sockaddr*)(&clientSockAddrIn), &addrlen);
            if(clientSockFd == -1) {
                cout << "here";
                cerr << "Fail to accept client: " << errno << endl;
                exit(-1);
            }

            // initialze everything needed by client including building connection, set env variable, set newReadFds, set validFds 
            initClient(clientSockFd, clientSockAddrIn);

            printWelcome(clientSockFd);

            // the login message of current logged in user is broadcasted to it.
            broadcast(clientSockFd, LOGIN);

            printPrompt(clientSockFd);
        }

        // process the clients here, check whether a client is entering message to shell(set the readFd bit).
        // the readFd bit is set until the client exit the shell
        for(auto client: clients) {
            cout << "look at the next client\n";
            // cout << client.clientSockFd << " " << FD_ISSET(client.clientSockFd, &readFds) << endl;
            if(FD_ISSET(client -> clientSockFd, &readFds)) {
                cout << "reading\n";
                char rawCommand[BUFFERSIZE] = ""; // {0, 0, 0, 0}
                int byteRead = read(client -> clientSockFd, rawCommand, BUFFERSIZE);
                cout << byteRead << '\n';

                if(byteRead == -1) {
                    string err = "Server fail to read message from client";
                    write(client -> clientSockFd, err.c_str(), err.size());
                    exit(-1);
                } 
                // up to this point, the server read the message from the client

                // the server now start to process the command sent from the client
                vector<string> parsedCommand = parseCommand(rawCommand);

                // reset the env varaibles before execute the next command
                cout << "restoring env\n";
                restoreClientEnv(client -> clientSockFd);


                cout << "====================START=====================\n";
                int clientState = npshell(parsedCommand, client -> clientSockFd);
                // close the fd and all resources allocated by the client
                cout << "=====================END======================\n\n\n";


                if(clientState == CLIENT_EXIT) {
                    broadcast(client -> clientSockFd, LOGOUT);
                    cleanUpClient(client -> clientSockFd);
                } 
            } 
        }

        readFds = newReadFds;
    }
}

int initFdTable(int serverSockFd) {
    int fdTableSize = getdtablesize();

    // zero out all dat in the validFds
    FD_ZERO(&validFds);

    // set the serverSockFd;
    FD_SET(serverSockFd, &validFds);

    // mark the readFds[serverSockFd], so that the server will proccess itself
    FD_SET(serverSockFd, &readFds);

    return fdTableSize;
}

void initClient(int clientSockFd, struct sockaddr_in clientSockAddrIn) {

    // set the validFds[clientSock]
    FD_SET(clientSockFd, &validFds);
    FD_SET(clientSockFd, &newReadFds);

    // initialize Client struct
    Client* client = new Client();
    client -> name = "(no name)";
    client -> clientSockFd = clientSockFd;
    client -> envVars["PATH"] = "bin:.";
    client -> address = clientSockAddrIn.sin_addr;
    client -> port = ntohs(clientSockAddrIn.sin_port);
    client -> id = getIdFromPool();

    clients.push_back(client);
}

void printWelcome(int clientSockFd) {
    string msg = "****************************************\n** Welcome to the information server. **\n****************************************\n";

    write(clientSockFd, msg.c_str(), msg.size());   
}

string getLoginString(int clientSockFd) {
    Client client = *getClientByFd(clientSockFd);
    string msg = "*** User '" + client.name + "' entered from " + inet_ntoa(client.address) + ":" + to_string(client.port) + ". ***\n";
    // write(clientSockFd, msg.c_str(), msg.size());
    return msg;
}

string getLogOutString(int clientSockFd) {
    Client client = *getClientByFd(clientSockFd);
    string msg = "*** User '" + client.name + "' left. ***\n";
    return msg;
}

Client* getClientByFd(int clientSockFd) {
    for(auto c: clients) {
        if(c -> clientSockFd == clientSockFd)
            return c;
    }
    return nullptr;
}

void printPrompt(int clientSockFd) {
    string msg = "% ";
    write(clientSockFd, msg.c_str(), msg.size());
}

// print logout message, close fd, update the clients vector
void cleanUpClient(int clientSockFd) {
    vector<Client*> newClients;

    Client* client = getClientByFd(clientSockFd); 
    vector<int> newIdPool;
    
    for(auto& c: clients) {
        if(c -> clientSockFd != clientSockFd) {
            newClients.push_back(c);
        }
        if(c -> id != client -> id) {
            newIdPool.push_back(c -> id);
        }
    }

    vector<UserPipe> newUserPipes;
    for(auto& p: userPipes) {
        if(p.writerFd == clientSockFd || p.readerFd) {
            registeredPipe[{p.writerFd, p.readerFd}] = false;
            close(p.writeEnd);
            close(p.readEnd);
        }
        if(p.writerFd != clientSockFd && p.readerFd != clientSockFd)
            newUserPipes.push_back(p);
    }

    vector<Pipe> newTimer;
    for(auto& p: timer) {
        if(p.writerFd == clientSockFd || p.readerFd == clientSockFd){
            close(p.writeEnd);
            close(p.readEnd);
            continue;
        }
        newTimer.push_back(p);
    }

    free(client);

    idPool = newIdPool;
    clients = newClients;
    userPipes = newUserPipes;
    timer = newTimer;

    close(clientSockFd);
    FD_CLR(clientSockFd, &validFds);
    FD_CLR(clientSockFd, &readFds);
    FD_CLR(clientSockFd, &newReadFds);
}

void restoreClientEnv(int clientSockFd) {
    clearenv();

    auto client = *getClientByFd(clientSockFd);
    for(auto& k: client.envVars) {
        setenv(k.first.c_str(), k.second.c_str(), 1);
    }
}

void broadcast(int clientSockFd, int broadcastType, string info1, int id) {
    Client client = *getClientByFd(clientSockFd);

    string msg;
    if(broadcastType == LOGIN) {
        msg = getLoginString(clientSockFd);
    } else if(broadcastType == LOGOUT) {
        msg = getLogOutString(clientSockFd);
    } else if(broadcastType == NAME) {
        msg = getNameString(clientSockFd);
    } else if(broadcastType == YELL) {
        msg = getYellString(clientSockFd, info1);
    } else if(broadcastType == PIPEIN) {
        msg = getPipeInString(clientSockFd, id, info1);
    } else if(broadcastType == PIPEOUT) {
        msg = getPipeOutString(clientSockFd, id, info1);
    }

    for(auto client: clients) {
        if(FD_ISSET(client -> clientSockFd, &validFds)) {
            write(client -> clientSockFd, msg.c_str(), msg.size());
        }
    }
}

// get the new id and also push the new id to the pool
int getIdFromPool() {
    int id;
    if(idPool.size() == 0){
        id = 1;
    } else {
        id = idPool.size() + 1;
        for(int i = 0; i < idPool.size(); i++) {
            // that slot is empty
            if(idPool[i] != i + 1) {
                id = i + 1;
                break;
            }
        }
    }
    idPool.push_back(id);
    sort(idPool.begin(), idPool.end());
    return id;
}

void processWho(int clientSockFd) {
    string header = "<ID>\t<nickname>\t<IP:port>\t<indicate me>\n";
    write(clientSockFd, header.c_str(), header.size());

    int curId = getClientByFd(clientSockFd) -> id;

    sort(idPool.begin(), idPool.end());
    for(auto id: idPool) {
        Client* c = getClientById(id);
        string msg = to_string(c -> id) + '\t' + c -> name + '\t' + inet_ntoa(c -> address) + ":" + to_string(c -> port);
        if(id == curId)
            msg += "\t<-me";
        msg += '\n';
        write(clientSockFd, msg.c_str(), msg.size());
    }

    // sort(clients.begin(), clients.end(), [](const Client* a, const Client* b) {
    //     return a -> id < b -> id;
    // });  

    // for(auto& c: clients) {
    //     cout << c -> name << " " << to_string(c -> id) << '\n';
    // }

    // cout << flag1 << '\n';

    // string msg;
    // for(auto& c: clients) {
    //     msg += to_string(c -> id) + "\t" + c -> name + "\t" + inet_ntoa(c -> address) + ":" + to_string(c -> port);
    //     if(c -> clientSockFd == clientSockFd) {
    //         msg += "\t<-me";
    //     }
    //     msg += "\n";
    //     // cout << msg;
    // }
    // cout << msg << '\n';
    // int flag2 = write(clientSockFd, msg.c_str(), msg.size());
    // cout << flag2 << '\n';
}

void processName(const vector<string>& extractedCommand, int clientSockFd) {
    if(extractedCommand.size() != 2) {
        return;
    }

    string name = extractedCommand[1];
    for(auto& client: clients) {
        if(name == client -> name) {
            string msg = "*** User '" + name + "' already exists. ***\n";
            write(clientSockFd, msg.c_str(), msg.size());
            return;
        }
    }

    Client* client = getClientByFd(clientSockFd);
    client -> name = name;

    broadcast(clientSockFd, NAME);
}

void processTell(const vector<string>& parsedCommand, int clientSockFd) {
    if(parsedCommand.size() <= 2)
        return;
    
    int readerId = stoi(parsedCommand[1]);

    Client* reader = getClientById(readerId);
    Client* writer = getClientByFd(clientSockFd);
    if(reader == nullptr) {
        string msg = "*** Error: user #" + to_string(readerId) +" does not exist yet. ***\n";
        write(clientSockFd, msg.c_str(), msg.size());
    } else {
        string msg = "*** " + writer -> name + " told you ***: ";
        for(int i = 2; i < parsedCommand.size(); i++) {
            msg += parsedCommand[i];
            if(i != parsedCommand.size() - 1)
                msg += " ";
        }
        msg += '\n';
        write(reader -> clientSockFd, msg.c_str(), msg.size());
    }
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

string getNameString(int clientSockFd) {
    Client* client = getClientByFd(clientSockFd);

    // weird datatype bug
    string msg = "*** User from ";
    msg += inet_ntoa(client -> address);
    msg += ":" + to_string(client -> port) + " is named '" + client -> name + "'. ***\n";
    return msg;
}

string getYellString(int clientSockFd, string yellString) {
    Client* client = getClientByFd(clientSockFd);
    string msg = "*** " + client -> name + " yelled ***: " + yellString + "\n";
    return msg;
}

Client* getClientById(int clientId) {
    for(auto& c: clients) {
        if(c -> id == clientId) {
            return c;
        }
    }
    return nullptr;
}

int checkUserPipeOut(int writerFd, int clientId) {
    Client* reader = getClientById(clientId);
    if(reader == nullptr) {
        return USER_NOTEXIST;
    } 
    if(registeredPipe[{writerFd, reader -> clientSockFd}])
        return PIPE_EXISTED;

    return PIPEOUT_OK;
}

string getUserNotExistString(int readerId) {
    string msg = "*** Error: user #" + to_string(readerId) + " does not exist yet. ***\n";
    return msg;
}

string getPipeExistedString(int clientSockFd, int readerId) {
    int writerId = getClientByFd(clientSockFd) -> id;

    string msg = "*** Error: the pipe #" + to_string(writerId) + "->#" + to_string(readerId) + " already exists. ***\n";
    return msg;
}

int checkUserPipeIn(int clientSockFd, int writerId) {
    Client* writer = getClientById(writerId);
    if(writer == nullptr) {
        return USER_NOTEXIST;
    }
    
    if(!registeredPipe[{writer -> clientSockFd, clientSockFd}])
        return PIPE_NOT_EXISTED;

    return PIPEIN_OK;
}

string getPipeNotExistedString(int clientSockFd, int writerId) {
    int readerId = getClientByFd(clientSockFd) -> id;

    string msg = "*** Error: the pipe #" + to_string(writerId) + "->#" + to_string(readerId) + " does not exist yet. ***\n";
    return msg;
}   

string getPipeInString(int clientSockFd, int writerId, string command) {
    Client* reader = getClientByFd(clientSockFd);
    Client* writer = getClientById(writerId);
    
    string msg = "*** ";
    msg += (reader -> name) + " (#" + to_string(reader -> id) + ") just received from " + writer -> name + " (#" + to_string(writer -> id) + ") by '" + command + "'***\n";
    return msg;
}

string getPipeOutString(int clientSockFd, int readerId, string command) {
    Client* reader = getClientById(readerId);
    Client* writer = getClientByFd(clientSockFd);

    string msg = "*** ";
    msg += (writer -> name) + " (#" + to_string(writer -> id) + ") just piped '" + command + "' to "+ reader -> name + " (#" + to_string(reader -> id) + ") ***\n";
    return msg;
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

void cleanUpUserPipe(int clientSockFd, int writerId) {
    vector<UserPipe> newUserPipes;
    int writerFd = getClientById(writerId) -> clientSockFd;

    for(auto p: userPipes) {
        if(clientSockFd == p.readerFd && writerFd == p.writerFd) {
            continue;
        }
        newUserPipes.push_back(p);
    }

    userPipes = newUserPipes;
}

void cleanUpPipe(int clientSockFd) {
    vector<Pipe> newTimer;

    for(auto& p: timer) {
        if(p.countdown == 0 && p.readerFd == clientSockFd)
            continue;
        newTimer.push_back(p);
    }

    timer = newTimer;
}