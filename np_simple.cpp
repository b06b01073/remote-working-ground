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
#define BUFFERSIZE 30000

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
};

vector<Pipe> timer;

ofstream history;

// use regex to match the numbered pipe operator
regex numberedPipe("\\|[0-9]+");
regex errorPipe("\\![0-9]+");

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

int initSocket(int);
void initSockAddrIn(struct sockaddr_in&, int);

void processRequest(int);

void cleanUpPipe();

// ./np_simple port_number
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
void npshell(int clientSockFd) {
    // cout << "% ";
    write(clientSockFd, "% ", strlen("% "));

    string rawCommand;

    // getline(cin, rawCommand) -> read from client
    while(true) {
        char rawCommand[BUFFERSIZE] = ""; // {0, 0, 0, 0}
        int byteRead = read(clientSockFd, rawCommand, BUFFERSIZE);
        if(byteRead == -1) {
            string err = "Server to write read message from client";
            write(clientSockFd, err.c_str(), err.size());
            exit(-1);
        }

        // the parseCommand will also strip the ending carriage return 
        vector<string> parsedCommand = parseCommand(rawCommand);


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

        while(idx < parsedCommand.size()) {
            // IO fd, the output is send to the client
            int input = -1;
            int output = -1;
            int errOutput = -1;

            vector<string> extractedCommand = extractCommand(parsedCommand, idx, input, output, errOutput);
            string lastCommand = extractedCommand.back();
            string firstCommand = extractedCommand[0];

            // operator is not considered as a part of command in my implementation
            if(lastCommand == "|" || lastCommand == ">" || regex_match(lastCommand, numberedPipe) || regex_match(lastCommand, errorPipe)) {
                extractedCommand.pop_back();
            }
            

            // up to here the default i, o, err = stdin, stdout, stderr

            if(firstCommand == "setenv") {
                processSetenv(extractedCommand, clientSockFd);
                break;
            } else if(firstCommand == "printenv") {
                processGetenv(extractedCommand, clientSockFd);
                break;
            } else if(firstCommand == "exit") {
                exit(0);
            } else if(lastCommand == "|") {
                openPipe(lastCommand);
            } else if(lastCommand == ">") {
                string fileName = parsedCommand[idx + 1];
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
            if(idx != parsedCommand.size() - 1 && (regex_match(lastCommand, numberedPipe) || regex_match(lastCommand, errorPipe))) {
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
    }
}

void processCommand(char** args, int& input, int& output, int& errOutput, string& lastCommand, int clientSockFd) {
    // need to modify and close the io fd if the io is not in its default settings, such as input != STDIN
    // cout << input << ' ' << output << ' ' << errOutput << ' ' << clientSockFd << '\n';

    // clientSockFd become the default Output fd here
    dup2(clientSockFd, STDOUT_FILENO);
    dup2(clientSockFd, STDERR_FILENO);
    close(clientSockFd);

    if(input != STDIN_FILENO){
        if(dup2(input, STDIN_FILENO) == -1) {
            perror("dup2 failed");
        }
        close(input);
    }
    if(output != clientSockFd) {
        if(dup2(output, STDOUT_FILENO) == -1) {
            perror("dup2 failed");
        }

        // IMPORTANT: we can only close the output here if output != errOutput, otherwise errOutput is a bad fd
        if(output != errOutput || errOutput == STDERR_FILENO)
            close(output);
    }
    if(errOutput != clientSockFd) {
        if(dup2(errOutput, STDERR_FILENO) == -1) {
            perror("dup2 failed");
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

void openPipe(string& lastCommand) {
    int fd[2];
    pipe(fd);
    
    // debug only
    // cout << fd[0] << ' ' << fd[1] << endl;

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


    // backlog = 0, no pending
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

    struct sockaddr_in clientSockAddrIn;
    socklen_t addrlen = sizeof(clientSockAddrIn);

    while(true) {

        // the accept block until the connection is built
        
        int clientSockFd = accept(serverSockFd, (struct sockaddr*)(&clientSockAddrIn), &addrlen);
        if(clientSockFd == -1) {
            cout << "here";
            cerr << "Fail to accept client: " << errno << endl;
            exit(-1);
        }

        // The server can send messge to client by the following code
        // string message = "hello";
        // auto client_message = message.c_str();
        // write(clientSockFd, client_message, strlen(client_message));


        int pid = fork();
        if(pid < 0) {
            cerr << "Fail to fork a child for client" << endl;
            exit(-1);
        } else if(pid == 0) {
            // close the serverSockFd, since the child only send the message to client.
            close(serverSockFd);

            npshell(clientSockFd);
            close(clientSockFd);

            // the child need to call exit, otherwise it will enter the next iteration of while loop
            exit(0);
        } else {
            // the clientSockFd is used by client not parent
            close(clientSockFd);
        }
    }
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