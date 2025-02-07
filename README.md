Information about the project can also be found in the PDF file.
Credits to https://json.nlohmann.me/ for the json library.

# **Key-Value Store with TTL and Synchronization**

This project implements a **distributed key-value store** with **Time-To-Live (TTL)** functionality and **synchronization** capabilities. The key-value store allows clients to set, get, and delete key-value pairs, with optional TTL for automatic expiration. The system supports **saving and restoring states, synchronization between clients, and logging for debugging and monitoring**.

## **Features**

### **Key-Value Operations:**
- **`SET`**: Store a key-value pair with a mandatory TTL.
- **`GET`**: Retrieve the value associated with a key.
- **`DELETE`**: Remove a key-value pair from the store.

### **State Management:**
- **`PUSH`**: Save the current state of the key-value store.
- **`POP`**: Restore the previous state of the key-value store.
- **`DELETESAVES`**: Delete all saved states.

### **Synchronization:**
- **`SYNC`**: Synchronize the key-value store with another client.

### **Logging:**
- All operations are logged to a file for debugging and monitoring.

### **TTL (Time-To-Live):**
- Keys can be set with a TTL, after which they are automatically deleted.

### **Distributed:**
- Multiple clients can connect to the server and synchronize their key-value stores.

---

## **Usage**

### **Prerequisites**
- **C++ Compiler**: Ensure you have a C++ compiler installed (e.g., `g++`).
- **JSON Library**: This project uses the **nlohmann/json** library for JSON parsing and serialization. Ensure it is installed and accessible.

### **Building the Project**
```bash
# Clone the repository or download the source code

# Compile the code using a C++ compiler
g++ -o kvstore KeyValueStore.cpp
```

### **Running the Server**
To start the server, run the following command:
```bash
./kvstore <[address:]port> [-d]
```
Where:
- `<[address:]port>`: The address and port to bind the server to. Defaults to `127.0.0.1` if no address is provided.
- `[-d]`: Optional flag to enable debug mode.

**Example:**
```bash
./kvstore 127.0.0.1:8080 -d
```

---

### **Running the Client**
Clients can connect to the server using the same address and port. The client supports the following commands:

#### **Set a key-value pair with a mandatory TTL:**
```bash
SET <key> <value> <TTL>
```
**Example:**
```bash
SET mykey myvalue 60
```

#### **Retrieve the value associated with a key:**
```bash
GET <key>
```
**Example:**
```bash
GET mykey
```

#### **Delete a key-value pair:**
```bash
DELETE <key>
```
**Example:**
```bash
DELETE mykey
```

#### **Get the current size of the cache:**
```bash
SIZE
```

#### **Print all key-value pairs in the cache:**
```bash
PRINTALL
```

#### **Save the current state of the key-value store:**
```bash
PUSH
```

#### **Restore the previous state of the key-value store:**
```bash
POP
```

#### **Delete all saved states:**
```bash
DELETESAVES
```

#### **Synchronize the key-value store with another client:**
```bash
SYNC
```

#### **Exit the client:**
```bash
QUIT
```

#### **Display the list of commands:**
```bash
HELP
```

---

## **Example Workflow**
```bash
# Start the server
./kvstore 127.0.0.1:8080

# Connect a client
./kvstore 127.0.0.1:8080

# Set a key-value pair
SET mykey myvalue 60

# Retrieve the value
GET mykey

# Synchronize with another client
SYNC

# Exit the client
QUIT
```

---

## **Code Structure**
- **`KeyValueStore` Class**: Manages the key-value store, including TTL, state management, and synchronization.
- **`CMDStructure` Struct**: Represents a command with its parameters.
- **`Response` Struct**: Represents the response from a command execution.
- **`InputParser` Function**: Parses raw input into a `CMDStructure`.
- **`distributionHandler` Function**: Handles client connections and synchronization.

---

## **Logging**
All operations are logged to a file in the `./logs/` directory. The log file is named based on the timestamp when the server was started.

---

## **Synchronization**
The **`SYNC`** command allows clients to synchronize their key-value stores. When a client issues the `SYNC` command, the server will find another connected client and propagate the key-value pairs to the requesting client.

---

## **TTL (Time-To-Live)**
Keys can be set with a TTL (in seconds). After the TTL expires, the key-value pair is **automatically deleted**. The system uses a **background thread** to handle TTL expiration.

---

## **State Management**
The key-value store supports **saving and restoring states** using the `PUSH` and `POP` commands. The `DELETESAVES` command can be used to delete all saved states.

---

## **Conclusion**
This **key-value store** is a **robust solution** for managing key-value pairs with **TTL and synchronization capabilities**. It is designed to be **simple to use** while providing powerful features for **state management and distributed synchronization**.

