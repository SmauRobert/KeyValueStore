#include <arpa/inet.h>
#include <cerrno>
#include <cstddef>
#include <iostream>
#include <fstream>
#include <cstring>
#include <cassert>
#include <sys/stat.h>
#include <unistd.h>
#include <string>
#include <thread>
#include <atomic>
#include <mutex>
#include <queue>
#include <ctime>
#include <map>
#include <stack>
#include "json.hpp"

using namespace std;
using json = nlohmann::json;

#define LOGMSG(format, ...) fprintf(LOG, format, ##__VA_ARGS__)

#define FOREACH_CMD(FUNC) \
    FUNC(ERROR) \
    FUNC(PUSH) \
    FUNC(POP) \
    FUNC(DELETESAVES) \
    FUNC(SIZE) \
    FUNC(PRINTALL) \
    FUNC(GET) \
    FUNC(DELETE) \
    FUNC(SET)

#define ENUM(CMD) CMD,
#define NAME(CMD) #CMD,

enum CMD { FOREACH_CMD(ENUM) };

vector<string> CMDEnumToString = { FOREACH_CMD(NAME) };
map<string, CMD> CMDStringToEnum = [] {
    map<string, CMD> m;
    for(int i = 0; i < CMDEnumToString.size(); i ++)
        m[CMDEnumToString[i]] = CMD(i);
    return m;
}();

struct CMDStructure {
    CMD CMDEnum;
    string key = "";
    string value = "";
    time_t TTL = 0;

    string toString() {
        return 
            "CMD: " + CMDEnumToString[CMDEnum] + 
            " | Key: "  + key +
            " | Value: " + value +
            " | TTL: " + to_string(TTL);
    }

    string Serialize() {
        return CMDEnumToString[CMDEnum] + (key != "" ? " " + key : "") + (value != "" ? " " + value : "") + (TTL > 0 ? " " + to_string(TTL) : "");
    }
};

struct Response {
    string value;
    bool success;
};

CMDStructure InputParser(string raw);

class KeyValueStore {
private:
    struct Entry {
        string key;
        time_t deleteTime;

        // priority queue = max-heap | we reverse the the operation to make it a min-heap
        bool operator <(const Entry &a) const {
            return deleteTime > a.deleteTime;
        }
    };

    stack<priority_queue<Entry>> recycleBin;
    thread recyclerThread;
    atomic<bool> recycling;

    ostream *notificationStream;
    char timeString[20];

    int sizeLimit, socketfd;
    stack<int> size;
    FILE *LOG;

    stack<map<string,string>> cacheSaves;
    #define cache cacheSaves.top()

    mutex mtx;

    Response Set(string key, string value, time_t TTL) {
        LOGMSG("[ set ] Checking validity of TTL\n");
        if(TTL <= 0) return { "Invalid TTL", false };

        int curr = 0;
        if(cache.find(key) != cache.end())
            curr = key.size() + cache[key].size();
        
        if(size.top() - curr + key.size() + value.size() > sizeLimit) {
            LOGMSG("[ set ] Pair of size %ld does not fit. Storing persistently\n", key.size() + value.size());
            string storagepath = "./temp/" + to_string(cacheSaves.size()) + "-" + string(timeString) + ".json";
            ifstream fin(storagepath);

            json object = json::object();
            fin >> object;
            fin.close();

            object[key] = value;

            ofstream fout(storagepath);
            fout << object.dump(4);
            fout.close();

        } else {
            LOGMSG("[ set ] Pair of size %ld does fit. Storing in memory\n", key.size() + value.size());
            size.top() = size.top() - curr + key.size() + value.size();
            cache[key] = value;
        }
        
        time_t deleteTime = time(nullptr) + TTL;
        LOGMSG("[ set ] Key %s to be removed at %ld\n", key.c_str(), deleteTime);
        recycleBin.top().push({key, deleteTime});

        return { "\"" + key + "\" = \"" + value + "\"", true };
    }

    Response Get(string key) {
        if(cache.find(key) != cache.end()) {
            LOGMSG("[ get ] Key found in memory\n");
            return { "\"" + cache[key] + "\"", true};
        }
        string storagepath = "./temp/" + to_string(cacheSaves.size()) + "-" + string(timeString) + ".json";
        ifstream fin(storagepath);

        json object = json::object();
        fin >> object;
        fin.close();

        if(object.find(key) != object.end()) {
            LOGMSG("[ get ] Key found in file\n");
            string value = object[key].dump().substr(1);
            value.pop_back();
            if(size.top() + key.size() + value.size() <= sizeLimit) {
                LOGMSG("[ get ] Moving pair to memory\n");
                size.top() += key.size() + value.size();
                cache[key] = value;
                
                object.erase(key);

                ofstream fout(storagepath);
                fout << object.dump(4);

                return { "\"" + cache[key] + "\"", true};
            }

            return { object[key].dump(), true };
        }
        
        return { "Key \"" + key + "\" not found", false };
    }

    Response Delete(string key) {
        if(cache.find(key) != cache.end()) {
            size.top() -= key.size() + cache[key].size();
            cache.erase(cache.find(key));
            return { "Key \"" + key + "\" deleted", true };
        }

        string storagepath = "./temp/" + to_string(cacheSaves.size()) + "-" + string(timeString) + ".json";
        ifstream fin(storagepath);

        json object = json::object();
        fin >> object;
        fin.close();

        if(object.find(key) == object.end())
            return { "Key \"" + key + "\" not found", false };

        object.erase(key);

        ofstream fout(storagepath);
        fout << object.dump(4);

        return { "Key \"" + key + "\" deleted", true };
    }

    Response Push() {
        LOGMSG("[ push ] Adding a new recyler bin\n");
        recycleBin.push(recycleBin.top());

        LOGMSG("[ push ] Saving cache\n");
        size.push(size.top());
        cacheSaves.push(cacheSaves.top());

        LOGMSG("[ push ] Creating new json file\n");
        string storagepath = "./temp/" + to_string(cacheSaves.size()) + "-" + string(timeString) + ".json";
        ofstream storage(storagepath);

        string laststoragepath = "./temp/" + to_string(cacheSaves.size() - 1) + "-" + string(timeString) + ".json";
        ifstream laststorage(laststoragepath);

        json object = json::object();
        laststorage >> object;

        storage << object.dump(4);
       
        return { "Cache state saved", true };
    }

    Response Pop() {
        if(cacheSaves.size() < 2) {
            return { "No saved state to reverse to", false };
        }

        recycleBin.pop();

        string storagepath = "./temp/" + to_string(cacheSaves.size()) + "-" + string(timeString) + ".json";
        remove(storagepath.c_str());

        cacheSaves.pop();
        size.pop();

        return { "Cache reversed to last saved state", true };
    }

    Response DeleteSaves() {
        string storagepath = "./temp/" + to_string(cacheSaves.size()) + "-" + string(timeString) + ".json";
        ifstream fin(storagepath);

        json object = json::object();
        fin >> object;
        fin.close();

        for(int i = 0; i <= cacheSaves.size(); i ++) {
            string storagepath = "./temp/" + to_string(i) + "-" + string(timeString) + ".json";
            remove(storagepath.c_str());
            LOGMSG("[ delete saves ] Removed %s\n", storagepath.c_str());
        }

        recycling = false;
        recyclerThread.join();

        priority_queue<Entry> tempRecycle = recycleBin.top();
        recycleBin = stack<priority_queue<Entry>>();
        recycleBin.push(tempRecycle);

        recycling = true;
        recyclerThread = thread(&KeyValueStore::RecycleBin, this);

        int s = size.top();
        size = stack<int>();
        size.push(s);

        map<string, string> tempCache = cacheSaves.top();
        cacheSaves = stack<map<string,string>>();
        cacheSaves.push(tempCache);

        storagepath = "./temp/" + to_string(cacheSaves.size()) + "-" + string(timeString) + ".json";
        ofstream storage(storagepath);

        storage << object.dump(4);  

        return { "Cache saves deleted", true };
    }

    Response Size() {
        return { to_string(size.top()) + " / " + to_string(sizeLimit) + " bytes", true };
    }

    Response PrintAll() {
        string s = "";
        if(cache.empty()) s.append("Cache is empty\n");
        else {
            s.append("Cache contents:\n");
            for(auto entry : cache)
                s.append(" - \"" + entry.first + "\" = \"" + entry.second + "\"\n");
        }

        string storagepath = "./temp/" + to_string(cacheSaves.size()) + "-" + string(timeString) + ".json";
        ifstream fin(storagepath);

        json object = json::object();
        fin >> object;
        fin.close();

        if(object.empty()) s.append("Persistent storage is empty\n");
        else {
            s.append("Persistent storage:\n");
            for(auto& [key, value] : object.items()) {
                s.append(" - \"" + key + "\" = " + value.dump() + '\n');
            }
        }

        s.pop_back();
        return { s, true };
    }

    void RecycleBin() {
        Entry temp;
        Response resp;

        while(recycling) {
            if(recycleBin.top().empty()) {
                sleep(1);
                continue;
            }

            temp = recycleBin.top().top();
            if(temp.deleteTime > time(nullptr)) {
                sleep(1);
                continue;
            }

            resp = Handler({ DELETE, temp.key, "", 0});
            if(notificationStream && resp.success) (*notificationStream) << resp.value + '\n';
            recycleBin.top().pop(); 
        }
    }

    void SendStacks(int depth) {
        if(recycleBin.size() == 0) {
            LOGMSG("Reached bottom of stack. Going back\n");
            return;
        }
        
        LOGMSG("Poping stack level: %s\n", to_string(recycleBin.size()).c_str());
        priority_queue<Entry> tempRecycle = recycleBin.top();
        map<string, string> tempCache = cacheSaves.top();

        recycleBin.pop();
        cacheSaves.pop();

        SendStacks(depth);

        recycleBin.push(tempRecycle);
        cacheSaves.push(tempCache);

        string storagepath = "./temp/" + to_string(cacheSaves.size()) + "-" + string(timeString) + ".json";
        ifstream fin(storagepath);

        json object = json::object();
        fin >> object;
        fin.close();

        priority_queue<Entry> q = tempRecycle;
        while(!q.empty()) {
            Entry e = q.top();
            q.pop();
            
            string value = "";
            if(cache.find(e.key) != cache.end()) {
                value = cache[e.key];
            } else if(object.find(e.key) != object.end()) {
                value = object[e.key].dump().substr(1);
                value.pop_back();
            } else continue;

            time_t curr = time(NULL);
            CMDStructure setcmd = { SET, e.key, value, e.deleteTime - curr};
            LOGMSG("[ handler ] propagating command %s\n", setcmd.toString().c_str());
            string temp = setcmd.Serialize();
            int size = temp.size();
            write(socketfd, &size, sizeof(size));
            write(socketfd, temp.c_str(), size);
        }

        if(recycleBin.size() < depth) {
            CMDStructure pushcmd = { PUSH, "", "", 0 };
            LOGMSG("[ handler ] propagating command %s\n", pushcmd.toString().c_str());
            string temp = pushcmd.Serialize();
            int size = temp.size();
            write(socketfd, &size, sizeof(size));
            write(socketfd, temp.c_str(), size); 
        }


        LOGMSG("Pushing stack level: %s\n", to_string(recycleBin.size()).c_str());
    }
public:
    KeyValueStore(int fd, size_t limit, ostream* stream) : sizeLimit(limit), recycling(true), notificationStream(stream), socketfd(fd) { 
        time_t curr = time(NULL);
        tm* instanceTime = localtime(&curr);

        char timeformat[] = "%d%m%y(%H:%M:%S)";
        strftime(timeString, 32, timeformat, instanceTime);

        string filepath = "./logs/KVStore-" + string(timeString) + ".log";

        if(access("./logs", F_OK) != 0) {
            mkdir("./logs", 0777);
        }

        LOG = fopen(filepath.c_str(), "a");
        assert(LOG != NULL);

        LOGMSG("[ constructor ] Initialized KVStore\n");

        cacheSaves.push(map<string, string>());

        size.push(0);
        recycleBin.push(priority_queue<Entry>());
        recyclerThread = thread(&KeyValueStore::RecycleBin, this);

        LOGMSG("[ constructor ] Started recyler thread\n");

        if(access("./temp", F_OK) != 0) {
            mkdir("./temp", 0777);
        }

        string storagepath = "./temp/" + to_string(cacheSaves.size()) + "-" + string(timeString) + ".json";
        ofstream storage(storagepath);

        json object = json::object();
        storage << object.dump(4);    
    }
    
    ~KeyValueStore() {
        LOGMSG("[ destructor] Waiting on recycler threads\n");
        recycling = false;
        recyclerThread.join();

        LOGMSG("[ destructor] Joined all recycler threads\n");

        for(int i = 0; i <= cacheSaves.size(); i ++) {
            string storagepath = "./temp/" + to_string(i) + "-" + string(timeString) + ".json";
            remove(storagepath.c_str());
            LOGMSG("[ destructor ] Removed %s\n", storagepath.c_str());
        }

        LOGMSG("[ destructor ] Destructed KVStore\n");
    }

    void clearSave() {
        while(!recycleBin.top().empty()) {
            Handler({ DELETE, recycleBin.top().top().key, "", 0});
            recycleBin.top().pop(); 
        }
    }

    void SendData() {
        // sending ALL data to socketfd
        recycling = false;
        recyclerThread.join();

        cout << "Stopped deleting data. Sending data... ( do not press anything )\n";
        SendStacks(recycleBin.size());

        cout << "Restarting recyler thread\n";
        recycling = true;
        recyclerThread = thread(&KeyValueStore::RecycleBin, this);

        // finished sending data
        char eot = 0x04;
        int size = sizeof(eot);
        write(socketfd, &size, sizeof(size));
        write(socketfd, &eot, size);

        cout << "Finished sending data. You may now continue\n";
    }

    Response Handler(CMDStructure cmd, bool propagate = false) {
        mtx.lock();
        LOGMSG("[ handler ] locked the critical section\n");
        Response resp;
        bool modifiable = false;
        switch(cmd.CMDEnum) {
            case SET: 
                resp = Set(cmd.key, cmd.value, cmd.TTL);
                modifiable = true;
                break;        
            case GET: 
                resp = Get(cmd.key);
                break;        
            case DELETE: 
                resp = Delete(cmd.key);
                modifiable = true;
                break;        
            case PUSH:
                resp = Push();
                modifiable = true;
                break;        
            case POP:
                resp = Pop();
                modifiable = true;
                break;        
            case DELETESAVES:
                resp = DeleteSaves();
                modifiable = true;
                break;        
            case SIZE:
                resp = Size();
                break;        
            case PRINTALL:
                resp = PrintAll();
                break;        
            default: 
                resp ={ cmd.toString(), false };
                break;
        }
        if(propagate && resp.success && modifiable) {
            LOGMSG("[ handler ] propagating command %s\n", cmd.toString().c_str());
            string temp = cmd.Serialize();
            write(socketfd, temp.c_str(), temp.size());
        }
        mtx.unlock();
        LOGMSG("[ handler ] unlocked the critical section\n");
        return resp;
    }

    friend CMDStructure InputParser(string raw) {
        CMDStructure cmd = { ERROR, "", "", 0 };
        int p = raw.find(' ');
        string temp = raw.substr(0, p);
        
        if(CMDStringToEnum.find(temp) == CMDStringToEnum.end())
            return { ERROR, "", "", 0 };
        
        cmd.CMDEnum = CMDStringToEnum[temp];

        if(p == raw.npos ^ cmd.CMDEnum < GET) return { ERROR, "", "", 0 };
        if(p == raw.npos) return cmd;

        raw = raw.substr(p + 1);
        p = raw.find(' ');
        cmd.key = raw.substr(0, p);

        if(p == raw.npos ^ cmd.CMDEnum < SET) return { ERROR, "", "", 0 };
        if(p == raw.npos) return cmd;

        raw = raw.substr(p + 1);
        p = raw.find(' ');
        cmd.value = raw.substr(0, p);

        if(p == raw.npos) return { ERROR, "", "", 0 };

        raw = raw.substr(p + 1);
        p = raw.find(' ');
        if(p != raw.npos) return { ERROR, "", "", 0 }; 

        cmd.TTL = atoi(raw.c_str());
        if(cmd.TTL <= 0) return { ERROR, "", "", 0 }; 
        
        return cmd;
    }

    #undef cache
};

#define DEBUGMSG(format, ...) if(DEBUG) fprintf(stderr, format, ##__VA_ARGS__)
bool DEBUG = false;

sockaddr_in StrToAddr(char *str) {
    char ADDR[18] = "127.0.0.1";
    int PORT = 0;
    
    char* sep = strchr(str, ':');
    if(sep == NULL) PORT = atoi(str);
    else {
        PORT = atoi(sep + 1);
        sep[0] = '\0';
        strcpy(ADDR, str);
    } 

    return {
        AF_INET,
        htons(PORT),
        in_addr {
            inet_addr(ADDR)
        }
    };
}

char *conv_addr(struct sockaddr_in address) {
    static char str[25];
    char port[7];

    /* adresa IP a clientului */
    strcpy(str, inet_ntoa(address.sin_addr));
    /* portul utilizat de client */
    bzero(port, 7);
    sprintf(port, ":%d", ntohs(address.sin_port));
    strcat(str, port);
    return (str);
}

void distributionHandler(int socketfd) {
    pid_t pid = fork();
    assert(pid != -1);

    if(pid != 0) return;

    close(0);

    time_t curr = time(NULL);
    tm* time = localtime(&curr);

    char fileformat[] = "./logs/distributor-%d%m%y(%H:%M).log";
    char filepath[64] = { 0 };

    strftime(filepath, 64, fileformat, time);

    if(access("./logs", F_OK) != 0) {
        mkdir("./logs", 0777);
    }

    FILE *LOG = fopen(filepath, "w");
    assert(LOG != NULL);

    assert(setsid() != -1);
    LOGMSG("[ status ] Detached successfully\n");

    assert(listen(socketfd, 5) == 0);
    LOGMSG("[ status ] Started up multiplexing server\n");

    fd_set readfds, actfds;
    struct timeval tv = { 1, 0 };

    FD_ZERO(&actfds);
    FD_SET(socketfd, &actfds);

    int clientCount = 0;
    bool running = true;

    int maxfd = socketfd;
    while(running) {
        bcopy((char *)&actfds, (char *)&readfds, sizeof(readfds));

        assert(select(maxfd + 1, &readfds, NULL, NULL, &tv) >= 0);

        if(FD_ISSET(socketfd, &readfds)) {
            struct sockaddr_in clientAddr;
            int clientAddrLen = sizeof(clientAddr);

            int clientfd = accept(socketfd, (struct sockaddr*)&clientAddr, (socklen_t *)&clientAddrLen);
            if(maxfd < clientfd) maxfd = clientfd;

            LOGMSG("[ connection ] New client with fd #%d from address %s\n", clientfd, conv_addr(clientAddr));

            FD_SET(clientfd, &actfds);
            clientCount ++;
        }

        for(int fd = 3; fd <= maxfd; fd ++)
            if(fd != socketfd && FD_ISSET(fd, &readfds)) {
                char buffer[256] = { 0 };
                int bytes = read(fd, buffer, sizeof(buffer));

                if(bytes == 0) {
                    LOGMSG("[ connection ] Client disconnected with fd #%d\n", fd);
                    close(fd);
                    FD_CLR(fd, &actfds);
                    clientCount --;
                    if(clientCount == 0) running = false;

                    continue;
                }

                if(bytes < 0) continue;

                if(strcmp(buffer, "SYNC") == 0) {
                    bool found = clientCount > 1;
                    write(fd, &found, sizeof(found));

                    if(!found) continue;
                    int syncerfd;
                    for (syncerfd = 3; syncerfd <= maxfd; syncerfd++)
                        if (syncerfd != socketfd && syncerfd != fd && FD_ISSET(syncerfd, &actfds))
                            break;

                    write(syncerfd, buffer, sizeof(buffer));

                    while(true) {
                        int size;
                        read(syncerfd, &size, sizeof(size));

                        char buffer[256] = { 0 };
                        int bytes = read(syncerfd, buffer, size);

                        if(bytes <= 0) break;

                        write(fd, &size, sizeof(size));
                        write(fd, buffer, bytes);

                        if(bytes == 1 && buffer[0] == 0x04) break;
                    }

                    continue;
                }

                LOGMSG("[ transmission ] From fd #%d: %s\n", fd, buffer);
                for(int writefd = 3; writefd <= maxfd; writefd ++)
                    if(writefd != socketfd && writefd != fd) {
                        write(writefd, buffer, bytes);
                    }
            }
    }

    LOGMSG("[ status ] Shutting down server\n");

    fclose(LOG);
    close(socketfd);
    exit(0);   
}

int main(int argc, char **argv) {
    if(argc != 2 && argc != 3) {
        cerr << "Usage: " << argv[0] << " <[address:]port> [-d]\n";
        return -1;
    }

    if(argc == 3) {
        if(strcmp(argv[2], "-d") == 0) DEBUG = true;
        else {
            cerr << "Usage: " << argv[0] << " <[address:]port> [-d]\n";
            return -1;
        }
    }

    int socketfd = socket(AF_INET, SOCK_STREAM, 0);
    assert(socketfd != -1);

    DEBUGMSG("[ status ] Created socket\n");

    int optval = 1;
    setsockopt(socketfd, SOL_SOCKET, SO_REUSEADDR,&optval,sizeof(optval));

    DEBUGMSG("[ status ] Set reusable option to socket\n");

    struct sockaddr_in serverAddr = StrToAddr(argv[1]);

    DEBUGMSG("[ status ] Attempting to bind socket to %s\n", conv_addr(serverAddr));
    if(bind(socketfd, (const struct sockaddr *)&serverAddr, sizeof(serverAddr)) == 0) {
        distributionHandler(socketfd);
    } else assert(errno == EADDRINUSE);

    DEBUGMSG("[ client - status ] Waiting for server to boot up\n");
    sleep(1);

    DEBUGMSG("[ client - status ] Attempting to connect to %s\n", conv_addr(serverAddr));

    socketfd = socket(AF_INET, SOCK_STREAM, 0);
    assert(connect(socketfd, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) != -1);
    DEBUGMSG("[ client - status ] Connected to server\n");

    fd_set readfds, actfds;
    struct timeval tv = { 1, 0 };

    FD_ZERO(&actfds);
    FD_SET(0, &actfds);
    FD_SET(socketfd, &actfds);

    bool running = true;

    ifstream fin(".config");
    size_t size = 0;
    fin >> size;

    KeyValueStore KVStore(socketfd, size, &cout);

    while(running) {
        bcopy((char *)&actfds, (char *)&readfds, sizeof(readfds));

        assert(select(socketfd + 1, &readfds, NULL, NULL, &tv) >= 0);
       
        if(FD_ISSET(0, &readfds)) {
            char buffer[256] = { 0 };
            int bytes = read(0, buffer, sizeof(buffer));

            if(bytes <= 0) continue;

            buffer[bytes - 1] = '\0';
            for(int i = 0; i < bytes; i ++)
                if(buffer[i] >= 'a' && buffer[i] <= 'z')
                    buffer[i] += 'A' - 'a';
            
            if(strcmp(buffer, "QUIT") == 0) {
                running = 0;
                continue;
            }

            if(strcmp(buffer, "--HELP") == 0) {
                cout << "\t\t\tCommand List\n\n1. SET <key> <value> <TTL>  | Sets the value of a key a defined period of time\n2. GET <key>                | Returns the value of a key\n3. DELETE <key>             | Deletes a key and its value\n4. SIZE                     | Returns the size of the cache\n5. PRINTALL                 | Prints all keys and their values\n6. PUSH                     | Saves the current state\n7. POP                      | Returns to previous saved state\n8. DELETESAVES              | Deletes all saved states\n9. SYNC                     | Synchronizes database\n10. QUIT                    | Quits the program\n11. HELP                    | Displays this list\n";
                continue;
            }

            if(strcmp(buffer, "SYNC") == 0) {
                write(socketfd, buffer, strlen(buffer));
                
                bool found;
                read(socketfd, &found, sizeof(found));

                if(!found) {
                    cout << "No other client to sync with\n";
                    continue;
                }

                cout << "Syncing...\n";

                while(true) {
                    int size;
                    read(socketfd, &size, sizeof(size));

                    char buffer[256] = { 0 };
                    int bytes = read(socketfd, buffer, size);

                    // cout << "Received: " << buffer << '\n';

                    if(bytes <= 0) continue;

                    if(bytes == 1 && buffer[0] == 0x04) break;

                    CMDStructure cmd = InputParser(buffer);
                    if(cmd.CMDEnum == ERROR) continue;

                    Response resp = KVStore.Handler(cmd);

                    cout << resp.value << '\n';

                    if(cmd.CMDEnum == PUSH)
                        KVStore.clearSave();
                }

                cout << "Finished syncing\n";
                continue;
            }

            CMDStructure cmd = InputParser(buffer);
            if(cmd.CMDEnum == ERROR) {
                cout << "Invalid command. Type 'HELP' to get a list of all valid commands\n";
                continue;
            }   

            Response resp = KVStore.Handler(cmd, true);
                       
            cout << resp.value << '\n';
        }
        
        if(FD_ISSET(socketfd, &readfds)) {
            char buffer[256] = { 0 };
            int bytes = read(socketfd, buffer, sizeof(buffer));

            if(bytes <= 0) continue;

            if(strcmp(buffer, "SYNC") == 0) {
                KVStore.SendData();
                continue;
            }

            CMDStructure cmd = InputParser(buffer);
            if(cmd.CMDEnum == ERROR) continue;

            Response resp = KVStore.Handler(cmd);

            cout << resp.value << '\n';
        }
    }

    close(socketfd);

    return 0;
}