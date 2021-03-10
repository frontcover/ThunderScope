#include "bridge.hpp"
#include "logger.hpp"

// Queues for Rx and Tx between C++ and Js
std::queue<EVPacket*> _gtxQueue;
std::queue<EVPacket*> _grxQueue;

// Mutexs for the queues
std::mutex _gtxLock;
std::mutex _grxLock;

/*******************************************************************************
 * FreePacket()
 *
 * Frees the packet and all data it points to.
 *
 * Arguments:
 *   EVPacket* packet - pointer to the packet.
 * Return:
 *   None
 ******************************************************************************/
inline void FreePacket(EVPacket* packet) {
    free(packet->data);
    free(packet);
}

/*******************************************************************************
 * PrintPacket()
 *
 * Prints the contents of a packet.
 *
 * Arguments:
 *   EVPacket* packet - pointer to the packet.
 * Return:
 *   None
 ******************************************************************************/
void PrintPacket(EVPacket* packet) {
    printf("PacketID_HEX: %X, Command: %d, DataSize: %d, Data: ",
           packet->packetID, packet->command,packet->dataSize);
    for(unsigned i = 0; i < packet->dataSize; i++) {
        printf("%X ",packet->data[i]);
    }
    printf("\n");
}

/*******************************************************************************
 * Bridge()
 *
 * Constructor for the bridge.
 *
 * Arguments:
 *   const char* pipeName
 *   std::queue<EVPacket*>& txQueue
 *   std::queue<EVPacket*>& rxQueue
 *   std::mutex& txLock
 *   std::mutex& rxLock
 * Return:
 *   None
 ******************************************************************************/
Bridge::Bridge(const char* pipeName, 
               std::queue<EVPacket*>& txQueue,
               std::queue<EVPacket*>& rxQueue,
               std::mutex& txLock,
               std::mutex& rxLock) :
_txQueue(txQueue),
_rxQueue(rxQueue),
_txLock(txLock),
_rxLock(rxLock)
{
    tx_run.store(false);
    rx_run.store(false);
#ifdef WIN32
    tx_hPipe = INVALID_HANDLE_VALUE;
    rx_hPipe = INVALID_HANDLE_VALUE;
#else
    tx_sock = -1;
    rx_sock = -1;
    client_tx_sock = -1;
#endif
#ifdef WIN32 //It seems that strcat_s is a windows thing. huh
    //set up TX Pipe/socket path
    strcat_s((char*)tx_connection_string,100,base_path);
    strcat_s((char*)tx_connection_string,100,pipeName);
    strcat_s((char*)tx_connection_string,100,"TX");
    //set up RX Pipe/socket path
    strcat_s((char*)rx_connection_string,100,base_path);
    strcat_s((char*)rx_connection_string,100,pipeName);
    strcat_s((char*)rx_connection_string,100,"RX");
#else
    //set up TX Pipe/socket path
    sprintf((char*)tx_connection_string,"%s%sTX",base_path,pipeName);
    //set up RX Pipe/socket path
    sprintf((char*)rx_connection_string,"%s%sRX",base_path,pipeName);
#endif
}

/*******************************************************************************
 * ~Bridge()
 *
 * Destructor for the bridge class.
 *
 * Arguments:
 *   None
 * Return:
 *   None
 ******************************************************************************/
Bridge::~Bridge() {
    RxStop();
    TxStop();
}

/*******************************************************************************
 * TxJob()
 *
 * Transmits packets in the tx queue.
 *
 * Arguments:
 *   None
 * Return:
 *   None
 ******************************************************************************/
void Bridge::TxJob() {
    //wait for a client (the electron app) to connect
#ifdef WIN32
    if(tx_hPipe == INVALID_HANDLE_VALUE) {
        return;
    }
    ConnectNamedPipe(tx_hPipe, NULL);
    INFO << "tx_pipe: client connected";
#else
    int rc;
    if(tx_sock == -1) {
        return;
    }
    //listen an accept a client (the electron app)
    rc = listen(tx_sock,10);
    if(0 != rc) {
        perror("tx_sock::listen(): ");
        return;
    }

    INFO << "tx_sock: listening for clients, waiting to aceept....";
    //accept the first client to connect
    struct sockaddr_un address;
    socklen_t addresslen = sizeof(sockaddr_un);
    client_tx_sock = accept(tx_sock,(struct sockaddr*)&address,&addresslen);
    if(client_tx_sock < 0) {
        perror("tx_sock::accept(): ");
        return;
    }
    INFO << "tx_sock: client connected";
#endif

    while(tx_run.load()) {
        //look into queue if there is anything to send
        _txLock.lock();
        if(_txQueue.empty()) {
            _txLock.unlock();
            //if nothing sleep for 500us
            std::this_thread::sleep_for(std::chrono::microseconds(500));
        } else {
            //else, send it
            EVPacket* txPacket = _gtxQueue.front(); //gets the pointer to the packet
            _gtxQueue.pop(); //removes it from the queue
            _txLock.unlock(); //free the queue so it can be used by something else
            uint16_t* txBuffCast = (uint16_t*)tx_buff;
            int packet_size = 6 + txPacket->dataSize;
            txBuffCast[0] = txPacket->command;
            txBuffCast[1] = txPacket->packetID;
            txBuffCast[2] = txPacket->dataSize;
            memcpy(tx_buff+6,txPacket->data,txPacket->dataSize);
#ifdef WIN32
            //send the packet over a named pipe
            unsigned long bytes_written;
            WriteFile(tx_hPipe,tx_buff,packet_size,&bytes_written,NULL);
#else
            //send the packet over a socket
            send(client_tx_sock,tx_buff,packet_size,0);
#endif
            free(txPacket);//free the packet
        }
    }
}

/*******************************************************************************
 * RxJob()
 *
 * recieves packets and processes them.
 *
 * Arguments:
 *   None
 * Return:
 *   None
 ******************************************************************************/
void Bridge::RxJob() {

#ifdef WIN32
    if(rx_hPipe == INVALID_HANDLE_VALUE)
        return;
    ConnectNamedPipe(rx_hPipe, NULL);
    INFO << "rx_pipe: client connected";
#else
    if(rx_sock == -1) {
        return;
    }
    //listen an accept a client (the electron app)
    int rc = listen(rx_sock,10);
    if(0 != rc) {
        perror("rx_sock::listen(): ");
        return;
    }

    INFO << "rx_sock: listening for clients, waiting to aceept....";
    //accept the first client to connect, no need to save it's fd since we will never
    //write to it
    struct sockaddr_un address;
    socklen_t addresslen = sizeof(sockaddr_un);
    client_rx_sock = accept(rx_sock,(struct sockaddr*)&address,&addresslen);
    if(client_rx_sock < 0) {
        perror("rx_sock::accept(): ");
        return;
    }
    INFO << "rx_sock: client connected";
#endif

    while(rx_run.load()) {
        //reading block until something is sent
#ifdef WIN32
        DWORD packet_size;
        int val;
        unsigned long bytes_read;
        //get the header
        val = ReadFile(rx_hPipe, rxBuff, 6, &bytes_read, NULL);
        if(val == 0) {
            //error
            int err = GetLastError();
            if(err == 234) { //err 234 is there is more data, not an error
                //do nothing
            } else if(err == 109) { //err 109 there were not enough bytes in the pipline
                std::this_thread::sleep_for(std::chrono::microseconds(500));
                continue; //error packet move onto next one
            } else {
                ERROR << "rx_pipe header_read: Error: " << GetLastError();
                break;
            }
        }
        //read the rest of the packet
        uint16_t dataSize = ((uint16_t*)(rxBuff))[2];
        if(dataSize < BRIDGE_BUFFER_SIZE - 6) {
            val = ReadFile(rx_hPipe, rxBuff + 6, dataSize, &bytes_read, NULL);
        } else {
            continue;
        }
        if(val == 0){
            //error
            int err = GetLastError();
            if(err == 234) { //err 234 is there is more data, not an error
                //do nothing
                std::this_thread::sleep_for(std::chrono::microseconds(500));
            } else if(err == 109) { //err 109 there were not enough bytes in the pipline
                std::this_thread::sleep_for(std::chrono::microseconds(500));
                continue; //error packet move onto next one
            } else {
                INFO << "rx_pipe data_read: Error: " << GetLastError();
                break;
            }
        }
        packet_size = 6 + dataSize;

#else
        ssize_t packet_size;
        packet_size = recv(client_rx_sock,rxBuff,BRIDGE_BUFFER_SIZE,0);
        if(packet_size > 0) {
            INFO << "rx_sock: Packet Size: " << (int)packet_size << " Message:" << rxBuff;
        } else if (packet_size == -1) {
            INFO << "rx_sock: Client has disconnected....";
            rx_run.store(false);
        } else {
            //if there is nothing, sleep for 500us
            std::this_thread::sleep_for(std::chrono::microseconds(500));
            continue;
        }
#endif
        //process whatever is sent (for now just print it)
        printf("Packet Size: %d, Packet Info:\n",(int)packet_size);
        for(int i = 0; i < (int)packet_size; i++)
            printf("%X ",rxBuff[i]);
        printf("\n");

        uint16_t* rxBuff16 = (uint16_t*) rxBuff;
        uint8_t* rxBuffData = (uint8_t*) (rxBuff + 6);
        //reconstruct packet struct
        EVPacket* rxPacket = (EVPacket*)malloc(sizeof(EVPacket));
        //get the header
        rxPacket->command = rxBuff16[0];
        rxPacket->packetID = rxBuff16[1];
        rxPacket->dataSize = rxBuff16[2];
        //check that the dataSize is valid (less than or equal to BUFF_SIZE - 6)
        if(rxPacket->dataSize <= BRIDGE_BUFFER_SIZE - 6) {
            rxPacket->data = (uint8_t*)malloc(rxPacket->dataSize);
            memcpy(rxPacket->data,rxBuffData,rxPacket->dataSize);
        } else {
            // TODO: this is a transmission error for now, until we get multiple
            // packet payloads enabled
            rxPacket->dataSize = 1;
            rxPacket->data = (uint8_t*)malloc(1);
        }
        //for now just print and free the packet
        PrintPacket(rxPacket);
        FreePacket(rxPacket);
        _rxLock.lock();
        //process whatever it is
        // Operate on the packet that was recieved from Electron
        _rxLock.unlock();
    }
}

/*******************************************************************************
 * TxStart()
 *
 * Initializes the tx bridge and starts the tx worker thread.
 *
 * Arguments:
 *   None
 * Return:
 *   int - 0 on success, err on failure
 ******************************************************************************/
int Bridge::TxStart() {
    if (tx_run.load() == true) {
        TxStop();
    }

    int err = InitTxBridge();
    if (err) {
        return err;
    }
    INFO << "Init'd Tx Bridge";

    tx_run.store(true);
    tx_worker = std::thread(&Bridge::TxJob, this);
    INFO << "Started Tx Worker";
    return 0;
}

/*******************************************************************************
 * RxStart()
 *
 * Initializes the rx bridge and starts the rx worker thread.
 *
 * Arguments:
 *   None
 * Return:
 *   int - 0 on success, err on failure
 ******************************************************************************/
int Bridge::RxStart() {
    if (rx_run.load() == true) {
        RxStop();
    }

    int err = InitRxBridge();
    if (err) {
        return err;
    }
    INFO << "Init'd Rx Bridge";

    rx_run.store(true);
    rx_worker = std::thread(&Bridge::RxJob, this);
    INFO << "Started Rx Worker";
    return 0;
}

#ifdef WIN32

/*******************************************************************************
 * TxStop()
 *
 * Closes the tx bridge and joins the worker thread.
 *
 * Arguments:
 *   None
 * Return:
 *   int - 0 on success
 ******************************************************************************/
int Bridge::TxStop() {
    tx_run.store(false);
    if(tx_worker.joinable()) {
        tx_worker.join();
    }

    if(INVALID_HANDLE_VALUE != tx_hPipe) {
        CloseHandle(tx_hPipe);
        tx_hPipe = INVALID_HANDLE_VALUE;
    }

    return 0;
}

/*******************************************************************************
 * RxStop()
 *
 * Closes the rx bridge and joins the worker thread.
 *
 * Arguments:
 *   None
 * Return:
 *   int - 0 on success
 ******************************************************************************/
int Bridge::RxStop() {
    rx_run(false);
    if(rx_worker.joinable()) {
        rx_worker.join();
    }

    if(INVALID_HANDLE_VALUE != rx_hPipe) {
        CloseHandle(rx_hPipe);
        rx_hPipe = INVALID_HANDLE_VALUE;
    }

    return 0;
}

/*******************************************************************************
 * InitTxBridge()
 *
 * Creates a named pipe for the tx bridge.
 *
 * Arguments:
 *   None
 * Return:
 *   int - 0 on success, error code on failure
 ******************************************************************************/
int Bridge::InitTxBridge() {
    if(tx_hPipe != INVALID_HANDLE_VALUE) {
        return 2;
    }
    tx_hPipe = CreateNamedPipe(tx_connection_string,
                               PIPE_ACCESS_DUPLEX | FILE_FLAG_FIRST_PIPE_INSTANCE,
                               // FILE_FLAG_FIRST_PIPE_INSTANCE is not needed
                               // but forces CreateNamedPipe(..) to fail if the
                               // pipe already exists...
                               PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
                               1,
                               4096 * 16,
                               4096 * 16,
                               NMPWAIT_USE_DEFAULT_WAIT,
                               NULL);

    if(tx_hPipe == INVALID_HANDLE_VALUE) {
        ERROR << "Failed To Create Tx Pipe at: " << tx_connection_string;
        return 1;
    }
    
    INFO << "Created Tx Pipe at: " << tx_connection_string;
    return 0;
}

/*******************************************************************************
 * InitRxBridge()
 *
 * Creates a named pipe for the rx bridge.
 *
 * Arguments:
 *   None
 * Return:
 *   int - 0 on success, error code on failure
 ******************************************************************************/
int Bridge::InitRxBridge() {
    if(rx_hPipe != INVALID_HANDLE_VALUE) {
        return 2;
    }
    rx_hPipe = CreateNamedPipe(rx_connection_string,
                               PIPE_ACCESS_DUPLEX | FILE_FLAG_FIRST_PIPE_INSTANCE,
                               // FILE_FLAG_FIRST_PIPE_INSTANCE is not needed
                               // but forces CreateNamedPipe(..) to fail if the
                               // pipe already exists...
                               PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
                               1,
                               4096 * 16,
                               4096 * 16,
                               NMPWAIT_USE_DEFAULT_WAIT,
                               NULL);

    if(rx_hPipe == INVALID_HANDLE_VALUE) {
        ERROR << "Failed To Create Rx Pipe at: " << rx_connection_string;
        return 1;
    }

    INFO << "Created Rx Pipe at: " << rx_connection_string;
    return 0;
}

#else

/*******************************************************************************
 * TxStop() on unix
 *
 * Closes the tx socket and joins the worker thread
 *
 * Arguments:
 *   None
 * Return:
 *   int - 0 on success, error code on failure
 ******************************************************************************/
int Bridge::TxStop() {
    tx_run.store(false);
    if(tx_worker.joinable()) {
        tx_worker.join();
    }

    if(-1 != tx_sock) {
        close(tx_sock);
        tx_sock = -1;
        client_tx_sock = -1;
        unlink(tx_connection_string);
    }

    return 0;
}

/*******************************************************************************
 * RxStop() on linux
 *
 * Closes the rx bridge and joins the worker thread.
 *
 * Arguments:
 *   None
 * Return:
 *   int - 0 on success
 ******************************************************************************/
int Bridge::RxStop() {
    rx_run.store(false);
    if(rx_worker.joinable()) {
        rx_worker.join();
    }

    if(-1 != rx_sock) {
        close(rx_sock);
        rx_sock = -1;
        unlink(rx_connection_string);
    }

    return 0;
}

/*******************************************************************************
 * InitTxBridge() on unix
 *
 * Creates a named pipe for the tx bridge.
 *
 * Arguments:
 *   None
 * Return:
 *   int - 0 on success, error code on failure
 ******************************************************************************/
int Bridge::InitTxBridge() {
    struct sockaddr_un name;

    //create the socket
    tx_sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if(tx_sock < 0) {
        perror("tx_sock::socket(): ");
        return 1;
    }
    //unlink the socket just in case the previous shutdown was not proper
    unlink(tx_connection_string);
    //set up the address/file location struct
    name.sun_family = AF_UNIX;
    int str_len = 0;
    while(tx_connection_string[str_len]) {
        name.sun_path[str_len] = tx_connection_string[str_len];
        str_len++;
    }
    name.sun_path[str_len] = 0;

    //bind the socket
    size_t size = SUN_LEN(&name);
    if(bind (tx_sock, (struct sockaddr *) &name, size) < 0) {
        perror("tx_sock::bind(): ");
        return 2;
    }

    INFO << "tx_sock created and bound on " << name.sun_path;
    return 0;
}

/*******************************************************************************
 * InitRxBridge() on unix
 *
 * Creates a named pipe for the rx bridge.
 *
 * Arguments:
 *   None
 * Return:
 *   int - 0 on success, error code on failure
 ******************************************************************************/
int Bridge::InitRxBridge() {
    struct sockaddr_un name;

    //create the socket
    rx_sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if(rx_sock < 0) {
        perror("rx_sock::socket(): ");
        return 1;
    }
    //unlink the socket just in case the previous shutdown was not proper
    unlink(rx_connection_string);
    //set up the address/file location struct
    name.sun_family = AF_UNIX;
    int str_len = 0;
    while(rx_connection_string[str_len]) {
        name.sun_path[str_len] = rx_connection_string[str_len];
        str_len++;
    }
    name.sun_path[str_len] = 0;

    //bind the socket
    size_t size = SUN_LEN(&name);
    if(bind (rx_sock, (struct sockaddr *) &name, size) < 0) {
        perror("rx_sock::bind(): ");
        return 2;
    }

    INFO << "rx_sock created and bound on " << name.sun_path;
    return 0;
}

#endif

/*******************************************************************************
 * runSocketTest()
 *
 * Creats a bridge, sends a test packet across the bridge and cleans up after
 * recieving a response.
 *
 * Arguments:
 *   None
 * Return:
 *   int - 0 on success, error code on failure
 ******************************************************************************/
void runSocketTest ()
{
    char in[10] = {};

    // Create packet
    EVPacket* testPacket = (EVPacket*)malloc(sizeof(EVPacket));
    testPacket->command = 1;
    testPacket->packetID = 0x0808;
    testPacket->dataSize = 5;
    testPacket->data = (uint8_t*)malloc(5);
    testPacket->data[0] = 1;
    testPacket->data[1] = 2;
    testPacket->data[2] = 3;
    testPacket->data[3] = 4;
    testPacket->data[4] = 5;

    // Pass packet to tx queue
    _gtxQueue.push(testPacket);
    Bridge* testBridge = new Bridge("testPipe",_gtxQueue,_grxQueue,_gtxLock,_grxLock);

    // start transfering
    testBridge->TxStart();
    testBridge->RxStart();

    std::cin >> in;

    delete testBridge;
}
