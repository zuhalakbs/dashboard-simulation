#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <cstring>
#include <errno.h>
#include <pthread.h>
#include <cstdlib>
#include <iomanip>
#include <ctime>
#include <fstream>
#include <vector>
#include <sstream>
#include <algorithm>
#include <map>
#include <deque> 
#include "config_reader.h"

using namespace std;

struct ClientData {
    int socket;
    int id;
};

struct Order {
    string orderId;
    int clientId;
    int clientSocket;
    string stockSymbol;
    string type; 
    double price;
    int quantity;
    int remainingQuantity;
    string status;
    string timestamp;
};


struct OrderBook {
    deque<Order> buyOrders;
    deque<Order> sellOrders;
};


struct Trade {
    string tradeId;
    string buyOrderId;
    string sellOrderId;
    int buyerClientId;
    int sellerClientId;
    string stockSymbol;
    double price;
    int quantity;
    string timestamp;
};


map<string, OrderBook> orderBooks;
pthread_mutex_t orderBookMutex = PTHREAD_MUTEX_INITIALIZER;
int orderIdCounter = 1;
pthread_mutex_t orderIdMutex = PTHREAD_MUTEX_INITIALIZER;

vector<Trade> trades;
pthread_mutex_t tradeMutex = PTHREAD_MUTEX_INITIALIZER;

map<int, int> clientSockets;
pthread_mutex_t clientSocketMutex = PTHREAD_MUTEX_INITIALIZER;

pthread_mutex_t clientCountMutex = PTHREAD_MUTEX_INITIALIZER;
int activeClients = 0;
bool serverRunning = true;
int globalServerSocket;

string getTimestamp() {
    time_t now = time(0);
    struct tm* timeinfo = localtime(&now);
    char buffer[20];
    strftime(buffer, sizeof(buffer), "%H:%M:%S", timeinfo);
    return string(buffer);
}

string generateOrderId() {
    pthread_mutex_lock(&orderIdMutex);
    int id = orderIdCounter++;
    pthread_mutex_unlock(&orderIdMutex);
    
    stringstream ss;
    ss << "ORD" << setfill('0') << setw(6) << id;
    return ss.str();
}

string generateTradeId() {
    static int tradeCounter = 1;
    stringstream ss;
    ss << "TRD" << setfill('0') << setw(6) << tradeCounter++;
    return ss.str();
}

void sendToClient(int clientSocket, const string& message) {
    string msg = message + "\n";
    send(clientSocket, msg.c_str(), msg.length(), 0);
}

string getDateStamp() {
    time_t now = time(0);
    struct tm* timeinfo = localtime(&now);
    char buffer[20];
    strftime(buffer, sizeof(buffer), "%Y-%m-%d", timeinfo);
    return string(buffer);
}

void saveOrderBook() {
    pthread_mutex_lock(&orderBookMutex);
    
    ofstream file("pending_orders.dat");
    if (!file.is_open()) {
        pthread_mutex_unlock(&orderBookMutex);
        return;
    }
    
    for (map<string, OrderBook>::const_iterator it = orderBooks.begin(); 
         it != orderBooks.end(); ++it) {
        const string& symbol = it->first;
        const OrderBook& book = it->second;
      
        for (deque<Order>::const_iterator buyIt = book.buyOrders.begin(); buyIt != book.buyOrders.end(); ++buyIt) {
            file << "BUY|" << buyIt->orderId << "|" << buyIt->clientId << "|"
                 << buyIt->stockSymbol << "|" << buyIt->price << "|" 
                 << buyIt->quantity << "|" << buyIt->remainingQuantity << "|"
                 << buyIt->status << "|" << buyIt->timestamp << endl;
        }
        
        for (deque<Order>::const_iterator sellIt = book.sellOrders.begin(); sellIt != book.sellOrders.end(); ++sellIt) {
            file << "SELL|" << sellIt->orderId << "|" << sellIt->clientId << "|"
                 << sellIt->stockSymbol << "|" << sellIt->price << "|" 
                 << sellIt->quantity << "|" << sellIt->remainingQuantity << "|"
                 << sellIt->status << "|" << sellIt->timestamp << endl;
        }
    }
    
    file.close();
    pthread_mutex_unlock(&orderBookMutex);
}

void loadOrderBook() {
    ifstream file("pending_orders.dat");
    if (!file.is_open()) {
        cout << "Bekleyen emir dosyası bulunamadı, yeni başlatılıyor." << endl;
        return;
    }
    
    pthread_mutex_lock(&orderBookMutex);
    
    string line;
    while (getline(file, line)) {
        stringstream ss(line);
        string type, orderId, clientIdStr, symbol, priceStr, quantityStr, 
               remainingStr, status, timestamp;
        
        getline(ss, type, '|');
        getline(ss, orderId, '|');
        getline(ss, clientIdStr, '|');
        getline(ss, symbol, '|');
        getline(ss, priceStr, '|');
        getline(ss, quantityStr, '|');
        getline(ss, remainingStr, '|');
        getline(ss, status, '|');
        getline(ss, timestamp, '|');
        
        Order order;
        order.orderId = orderId;
        order.clientId = atoi(clientIdStr.c_str());
        order.clientSocket = -1;
        order.stockSymbol = symbol;
        order.type = (type == "BUY") ? "AL" : "SAT";
        order.price = atof(priceStr.c_str());
        order.quantity = atoi(quantityStr.c_str());
        order.remainingQuantity = atoi(remainingStr.c_str());
        order.status = status;
        order.timestamp = timestamp;
        
        OrderBook& book = orderBooks[symbol];
        
        if (type == "BUY") {
            deque<Order>::iterator it = book.buyOrders.begin();
            while (it != book.buyOrders.end() && it->price > order.price) {
                ++it;
            }
            book.buyOrders.insert(it, order);
        } else {
            deque<Order>::iterator it = book.sellOrders.begin();
            while (it != book.sellOrders.end() && it->price < order.price) {
                ++it;
            }
            book.sellOrders.insert(it, order);
        }
    }
    
    file.close();
    pthread_mutex_unlock(&orderBookMutex);
    
    cout << "Bekleyen emirler yüklendi." << endl;
    
    int maxOrderId = 0;
    pthread_mutex_lock(&orderBookMutex);
    for (map<string, OrderBook>::const_iterator it = orderBooks.begin(); 
         it != orderBooks.end(); ++it) {
        const OrderBook& book = it->second;
        
        for (deque<Order>::const_iterator buyIt = book.buyOrders.begin(); 
             buyIt != book.buyOrders.end(); ++buyIt) {
            string idStr = buyIt->orderId.substr(3);
            int id = atoi(idStr.c_str());
            if (id > maxOrderId) maxOrderId = id;
        }
        
        for (deque<Order>::const_iterator sellIt = book.sellOrders.begin(); 
             sellIt != book.sellOrders.end(); ++sellIt) {
            string idStr = sellIt->orderId.substr(3);
            int id = atoi(idStr.c_str());
            if (id > maxOrderId) maxOrderId = id;
        }
    }
    pthread_mutex_unlock(&orderBookMutex);
    
    pthread_mutex_lock(&orderIdMutex);
    orderIdCounter = maxOrderId + 1;
    pthread_mutex_unlock(&orderIdMutex);
}

void* autoSaveOrderBook(void* arg) {
    while (serverRunning) {
        for (int i = 0; i < 30 && serverRunning; i++) {
            sleep(1);
        }
        
        if (serverRunning) {
            saveOrderBook();
        }
    }
    return NULL;
}

void matchOrders(Order& newOrder) {
    if (pthread_mutex_trylock(&orderBookMutex) != 0) {
        return;
    }
    
    OrderBook& book = orderBooks[newOrder.stockSymbol];
    
    if (newOrder.type == "AL") {
        while (!book.sellOrders.empty() && newOrder.remainingQuantity > 0) {
            Order& sellOrder = book.sellOrders.front();
            
            if (newOrder.price >= sellOrder.price) {
                int tradeQuantity = min(newOrder.remainingQuantity, sellOrder.remainingQuantity);
                double tradePrice = sellOrder.price;
                
                Trade trade;
                trade.tradeId = generateTradeId();
                trade.buyOrderId = newOrder.orderId;
                trade.sellOrderId = sellOrder.orderId;
                trade.buyerClientId = newOrder.clientId;
                trade.sellerClientId = sellOrder.clientId;
                trade.stockSymbol = newOrder.stockSymbol;
                trade.price = tradePrice;
                trade.quantity = tradeQuantity;
                trade.timestamp = getTimestamp();
                
                pthread_mutex_lock(&tradeMutex);
                trades.push_back(trade);
                pthread_mutex_unlock(&tradeMutex);
                
                newOrder.remainingQuantity -= tradeQuantity;
                sellOrder.remainingQuantity -= tradeQuantity;
                
                pthread_mutex_lock(&clientSocketMutex);
                
                if (clientSockets.find(newOrder.clientId) != clientSockets.end()) {
                    stringstream buyMsg;
                    buyMsg << "TRADE|" << trade.tradeId << "|ALIM|" 
                          << newOrder.stockSymbol << "|" << fixed << setprecision(2) << tradePrice 
                          << "|" << tradeQuantity << "|" << newOrder.orderId;
                    sendToClient(clientSockets[newOrder.clientId], buyMsg.str());
                }
                
                if (clientSockets.find(sellOrder.clientId) != clientSockets.end()) {
                    stringstream sellMsg;
                    sellMsg << "TRADE|" << trade.tradeId << "|SATIM|" 
                           << newOrder.stockSymbol << "|" << fixed << setprecision(2) << tradePrice 
                           << "|" << tradeQuantity << "|" << sellOrder.orderId;
                    sendToClient(clientSockets[sellOrder.clientId], sellMsg.str());
                }
                
                pthread_mutex_unlock(&clientSocketMutex);

                cout << "[" << getTimestamp() << "] İŞLEM - " << newOrder.stockSymbol 
                     << " " << tradeQuantity << " adet @ " << fixed << setprecision(2) 
                     << tradePrice << " TL (Alıcı: Client#" << newOrder.clientId 
                     << ", Satıcı: Client#" << sellOrder.clientId << ")" << endl;
                
                ofstream tradeFile("trades.log", ios::app);
                if (tradeFile.is_open()) {
                    tradeFile << getDateStamp() << " " << getTimestamp() 
                             << "|" << trade.tradeId << "|" << newOrder.stockSymbol 
                             << "|" << tradePrice << "|" << tradeQuantity
                             << "|Client#" << newOrder.clientId << "|Client#" << sellOrder.clientId << endl;
                    tradeFile.close();
                }
                
                if (sellOrder.remainingQuantity == 0) {
                    book.sellOrders.pop_front();
                }
            } else break;
        }
    } else { 
        while (!book.buyOrders.empty() && newOrder.remainingQuantity > 0) {
            Order& buyOrder = book.buyOrders.front();
            
            if (buyOrder.price >= newOrder.price) {
                int tradeQuantity = min(newOrder.remainingQuantity, buyOrder.remainingQuantity);
                double tradePrice = newOrder.price; 
                
                Trade trade;
                trade.tradeId = generateTradeId();
                trade.buyOrderId = buyOrder.orderId;
                trade.sellOrderId = newOrder.orderId;
                trade.buyerClientId = buyOrder.clientId;
                trade.sellerClientId = newOrder.clientId;
                trade.stockSymbol = newOrder.stockSymbol;
                trade.price = tradePrice;
                trade.quantity = tradeQuantity;
                trade.timestamp = getTimestamp();
                
                pthread_mutex_lock(&tradeMutex);
                trades.push_back(trade);
                pthread_mutex_unlock(&tradeMutex);
                
                newOrder.remainingQuantity -= tradeQuantity;
                buyOrder.remainingQuantity -= tradeQuantity;
                
                pthread_mutex_lock(&clientSocketMutex);
                
                if (clientSockets.find(buyOrder.clientId) != clientSockets.end()) {
                    stringstream buyMsg;
                    buyMsg << "TRADE|" << trade.tradeId << "|ALIM|" 
                          << newOrder.stockSymbol << "|" << fixed << setprecision(2) << tradePrice 
                          << "|" << tradeQuantity << "|" << buyOrder.orderId;
                    sendToClient(clientSockets[buyOrder.clientId], buyMsg.str());
                }
                
                if (clientSockets.find(newOrder.clientId) != clientSockets.end()) {
                    stringstream sellMsg;
                    sellMsg << "TRADE|" << trade.tradeId << "|SATIM|" 
                           << newOrder.stockSymbol << "|" << fixed << setprecision(2) << tradePrice 
                           << "|" << tradeQuantity << "|" << newOrder.orderId;
                    sendToClient(clientSockets[newOrder.clientId], sellMsg.str());
                }
                
                pthread_mutex_unlock(&clientSocketMutex);
                
                cout << "[" << getTimestamp() << "] İŞLEM - " << newOrder.stockSymbol 
                     << " " << tradeQuantity << " adet @ " << fixed << setprecision(2) 
                     << tradePrice << " TL (Alıcı: Client#" << buyOrder.clientId 
                     << ", Satıcı: Client#" << newOrder.clientId << ")" << endl;
                
                ofstream tradeFile("trades.log", ios::app);
                if (tradeFile.is_open()) {
                    tradeFile << getDateStamp() << " " << getTimestamp() 
                             << "|" << trade.tradeId << "|" << newOrder.stockSymbol 
                             << "|" << tradePrice << "|" << tradeQuantity
                             << "|Client#" << buyOrder.clientId << "|Client#" << newOrder.clientId << endl;
                    tradeFile.close();
                }
                
                if (buyOrder.remainingQuantity == 0) {
                    book.buyOrders.pop_front();
                }
            } else break;
        }
    }
    
    pthread_mutex_unlock(&orderBookMutex);
    saveOrderBook();
}

void addOrderToBook(const Order& order) {
    if (order.remainingQuantity > 0) {
        pthread_mutex_lock(&orderBookMutex);
        
        OrderBook& book = orderBooks[order.stockSymbol];
        
        if (order.type == "AL") {
            deque<Order>::iterator it = book.buyOrders.begin();
            while (it != book.buyOrders.end() && it->price > order.price) {
                ++it;
            }
            book.buyOrders.insert(it, order);
        } else {
            deque<Order>::iterator it = book.sellOrders.begin();
            while (it != book.sellOrders.end() && it->price < order.price) {
                ++it;
            }
            book.sellOrders.insert(it, order);
        }
        
        pthread_mutex_unlock(&orderBookMutex);
        saveOrderBook();
    }
}

void displayOrderBook() {
    if (pthread_mutex_trylock(&orderBookMutex) != 0) {
        cout << "Order book şu anda güncelleniyior, lütfen bekleyin..." << endl;
        return;
    }
    
    cout << "\n=== ORDER BOOK DURUMU ===" << endl;
    
    try {
        for (map<string, OrderBook>::const_iterator it = orderBooks.begin(); 
             it != orderBooks.end(); ++it) {
            const string& symbol = it->first;
            const OrderBook& book = it->second;
            
            if (!book.buyOrders.empty() || !book.sellOrders.empty()) {
                cout << "\n" << symbol << ":" << endl;
                cout << "  ALIŞ EMİRLERİ:" << endl;
                
                int count = 0;
                for (deque<Order>::const_iterator buyIt = book.buyOrders.begin(); 
                     buyIt != book.buyOrders.end() && count < 5; ++buyIt, ++count) {
                    cout << "    " << fixed << setprecision(2) << buyIt->price 
                         << " TL x " << buyIt->remainingQuantity << " adet (Client#" 
                         << buyIt->clientId << ")" << endl;
                }
                
                cout << "  SATIŞ EMİRLERİ:" << endl;
                count = 0;
                for (deque<Order>::const_iterator sellIt = book.sellOrders.begin(); 
                     sellIt != book.sellOrders.end() && count < 5; ++sellIt, ++count) {
                    cout << "    " << fixed << setprecision(2) << sellIt->price 
                         << " TL x " << sellIt->remainingQuantity << " adet (Client#" 
                         << sellIt->clientId << ")" << endl;
                }
            }
        }
    } catch (...) {
        pthread_mutex_unlock(&orderBookMutex);
        throw;
    }
    
    pthread_mutex_unlock(&orderBookMutex);
    cout << string(50, '-') << endl;
}
    

void displayTradeSummary() {
    pthread_mutex_lock(&tradeMutex);
    
    cout << "\n=== GÜNÜN İŞLEMLERİ ===" << endl;
    cout << string(80, '-') << endl;
    
    int todayTradeCount = 0;
    double todayVolume = 0;
    
    for (vector<Trade>::const_iterator it = trades.begin(); it != trades.end(); ++it) {
        todayTradeCount++;
        todayVolume += it->price * it->quantity;
        
        cout << it->timestamp << " " << it->stockSymbol 
             << " " << it->quantity << " adet @ " << fixed << setprecision(2) 
             << it->price << " TL (Alıcı: Client#" << it->buyerClientId 
             << ", Satıcı: Client#" << it->sellerClientId << ")" << endl;
    }
    
    cout << string(80, '-') << endl;
    cout << "Toplam İşlem: " << todayTradeCount << endl;
    cout << "Toplam Hacim: " << fixed << setprecision(2) << todayVolume << " TL" << endl;
    
    pthread_mutex_unlock(&tradeMutex);
}

void displayServerOrders() {
    ifstream file("server_orders.log");
    if (!file.is_open()) {
        cout << "\nEmir dosyası bulunamadı." << endl;
        return;
    }
    
    vector<string> orders;
    string line;
    while (getline(file, line)) {
        orders.push_back(line);
    }
    file.close();
    
    cout << "\n=== SON EMİRLER ===" << endl;
    cout << string(80, '-') << endl;
    
    int count = min(20, (int)orders.size());
    int start = orders.size() - count;
    
    for (int i = start; i < (int)orders.size(); i++) {
        stringstream ss(orders[i]);
        string timestamp, client, orderData;
        getline(ss, timestamp, '|');
        getline(ss, client, '|');
        getline(ss, orderData);

        stringstream orderSS(orderData);
        string cmd, symbol, type, price, quantity;
        getline(orderSS, cmd, '|');
        getline(orderSS, symbol, '|');
        getline(orderSS, type, '|');
        getline(orderSS, price, '|');
        getline(orderSS, quantity, '|');
        
        cout << timestamp << " " << client << " " 
             << symbol << " " << type << " " 
             << price << " TL x " << quantity << " adet" << endl;
    }
    cout << string(80, '-') << endl;
    cout << "Toplam " << count << " emir gösteriliyor." << endl;
}

void displayDailySummary() {
    ifstream file("server_orders.log");
    if (!file.is_open()) {
        cout << "\nEmir dosyası bulunamadı." << endl;
        return;
    }
    
    string today = getDateStamp();
    int totalOrders = 0;
    int buyOrders = 0;
    int sellOrders = 0;
    double totalVolume = 0;
    map<string, int> stockCounts;
    
    string line;
    while (getline(file, line)) {
        if (line.find(today) != string::npos) {
            totalOrders++;
            
            stringstream ss(line);
            string timestamp, client, orderData;
            getline(ss, timestamp, '|');
            getline(ss, client, '|');
            getline(ss, orderData);
            
            stringstream orderSS(orderData);
            string cmd, symbol, type, priceStr, quantityStr;
            getline(orderSS, cmd, '|');
            getline(orderSS, symbol, '|');
            getline(orderSS, type, '|');
            getline(orderSS, priceStr, '|');
            getline(orderSS, quantityStr, '|');
            
            if (type == "AL") buyOrders++;
            else if (type == "SAT") sellOrders++;
            
            stockCounts[symbol]++;
            
            double price = atof(priceStr.c_str());
            int quantity = atoi(quantityStr.c_str());
            totalVolume += price * quantity;
        }
    }
    file.close();
    
    cout << "\n=== GÜNLÜK ÖZET (" << today << ") ===" << endl;
    cout << string(50, '-') << endl;
    cout << "Toplam Emir: " << totalOrders << endl;
    cout << "Alış Emirleri: " << buyOrders << endl;
    cout << "Satış Emirleri: " << sellOrders << endl;
    cout << "Toplam İşlem Hacmi: " << fixed << setprecision(2) << totalVolume << " TL" << endl;
    cout << "\nHisse Bazında Dağılım:" << endl;
    
    for (map<string, int>::const_iterator it = stockCounts.begin(); 
         it != stockCounts.end(); ++it) {
        cout << "  " << it->first << ": " << it->second << " emir" << endl;
    }
    cout << string(50, '-') << endl;
}

void* handleClient(void* arg) {
    ClientData* clientData = (ClientData*)arg;
    int clientSocket = clientData->socket;
    int clientId = clientData->id;
    
    pthread_mutex_lock(&clientSocketMutex);
    clientSockets[clientId] = clientSocket;
    pthread_mutex_unlock(&clientSocketMutex);
    
    pthread_mutex_lock(&clientCountMutex);
    activeClients++;
    cout << "[" << getTimestamp() << "] Client #" << clientId 
         << " bağlandı (Aktif: " << activeClients << ")" << endl;
    pthread_mutex_unlock(&clientCountMutex);
    
    string welcome = "Server'a hoş geldiniz (Client #" + to_string(clientId) + ")\n";
    send(clientSocket, welcome.c_str(), welcome.length(), 0);
    
    char buffer[1024];
    while (true) {
        memset(buffer, 0, sizeof(buffer));
        
        ssize_t bytesReceived = recv(clientSocket, buffer, sizeof(buffer) - 1, 0);
        
        if (bytesReceived <= 0) {
            break;
        }
        
        buffer[bytesReceived] = '\0';

        for (int i = bytesReceived - 1; i >= 0; i--) {
            if (buffer[i] == '\n' || buffer[i] == '\r') {
                buffer[i] = '\0';
            } else {
                break;
            }
        }
        
        if (strcmp(buffer, "quit") == 0) {
            string goodbye = "Görüşmek üzere!\n";
            send(clientSocket, goodbye.c_str(), goodbye.length(), 0);
            break;
        }
        
        string msg(buffer);
        
        if (msg.substr(0, 5) == "EMIR|") {
            stringstream ss(msg);
            string cmd, symbol, type, priceStr, quantityStr;
            getline(ss, cmd, '|');
            getline(ss, symbol, '|');
            getline(ss, type, '|');
            getline(ss, priceStr, '|');
            getline(ss, quantityStr, '|');
            
            double price = atof(priceStr.c_str());
            int quantity = atoi(quantityStr.c_str());
            
            Order order;
            order.orderId = generateOrderId();
            order.clientId = clientId;
            order.clientSocket = clientSocket;
            order.stockSymbol = symbol;
            order.type = type;
            order.price = price;
            order.quantity = quantity;
            order.remainingQuantity = quantity;
            order.status = "PENDING";
            order.timestamp = getTimestamp();
            
            ofstream orderFile("server_orders.log", ios::app);
            if (orderFile.is_open()) {
                orderFile << getDateStamp() << " " << getTimestamp() 
                        << "|Client#" << clientId << "|" << msg << endl;
                orderFile.close();
            }
            
            cout << "[" << getTimestamp() << "] EMİR - Client #" << clientId 
                << ": " << symbol << " " << type << " " 
                << price << " TL x " << quantity << " adet" << endl;
            
            pthread_mutex_lock(&orderBookMutex);
            
            OrderBook& book = orderBooks[symbol];
            
            if (order.type == "AL") {
                while (!book.sellOrders.empty() && order.remainingQuantity > 0) {
                    Order& sellOrder = book.sellOrders.front();
                    
                    if (order.price >= sellOrder.price) {
                        int tradeQuantity = min(order.remainingQuantity, sellOrder.remainingQuantity);
                        double tradePrice = sellOrder.price;
                        
                        Trade trade;
                        trade.tradeId = generateTradeId();
                        trade.buyOrderId = order.orderId;
                        trade.sellOrderId = sellOrder.orderId;
                        trade.buyerClientId = order.clientId;
                        trade.sellerClientId = sellOrder.clientId;
                        trade.stockSymbol = symbol;
                        trade.price = tradePrice;
                        trade.quantity = tradeQuantity;
                        trade.timestamp = getTimestamp();
                        
                        pthread_mutex_lock(&tradeMutex);
                        trades.push_back(trade);
                        pthread_mutex_unlock(&tradeMutex);
                        
                        order.remainingQuantity -= tradeQuantity;
                        sellOrder.remainingQuantity -= tradeQuantity;
                        
                        pthread_mutex_lock(&clientSocketMutex);
                        
                        if (clientSockets.find(order.clientId) != clientSockets.end()) {
                            stringstream buyMsg;
                            buyMsg << "TRADE|" << trade.tradeId << "|ALIM|" 
                                  << symbol << "|" << fixed << setprecision(2) << tradePrice 
                                  << "|" << tradeQuantity << "|" << order.orderId;
                            sendToClient(clientSockets[order.clientId], buyMsg.str());
                        }
                        
                        if (clientSockets.find(sellOrder.clientId) != clientSockets.end()) {
                            stringstream sellMsg;
                            sellMsg << "TRADE|" << trade.tradeId << "|SATIM|" 
                                   << symbol << "|" << fixed << setprecision(2) << tradePrice 
                                   << "|" << tradeQuantity << "|" << sellOrder.orderId;
                            sendToClient(clientSockets[sellOrder.clientId], sellMsg.str());
                        }
                        
                        pthread_mutex_unlock(&clientSocketMutex);
                        
                        cout << "[" << getTimestamp() << "] İŞLEM - " << symbol 
                             << " " << tradeQuantity << " adet @ " << fixed << setprecision(2) 
                             << tradePrice << " TL (Alıcı: Client#" << order.clientId 
                             << ", Satıcı: Client#" << sellOrder.clientId << ")" << endl;
                        
                        ofstream tradeFile("trades.log", ios::app);
                        if (tradeFile.is_open()) {
                            tradeFile << getDateStamp() << " " << getTimestamp() 
                                     << "|" << trade.tradeId << "|" << symbol 
                                     << "|" << tradePrice << "|" << tradeQuantity
                                     << "|Client#" << order.clientId << "|Client#" << sellOrder.clientId << endl;
                            tradeFile.close();
                        }
                        
                        if (sellOrder.remainingQuantity == 0) {
                            book.sellOrders.pop_front();
                        }
                    } else {
                        break;
                    }
                }
            } else {
                while (!book.buyOrders.empty() && order.remainingQuantity > 0) {
                    Order& buyOrder = book.buyOrders.front();
                    
                    if (buyOrder.price >= order.price) {
                        int tradeQuantity = min(order.remainingQuantity, buyOrder.remainingQuantity);
                        double tradePrice = order.price;
                        
                        Trade trade;
                        trade.tradeId = generateTradeId();
                        trade.buyOrderId = buyOrder.orderId;
                        trade.sellOrderId = order.orderId;
                        trade.buyerClientId = buyOrder.clientId;
                        trade.sellerClientId = order.clientId;
                        trade.stockSymbol = symbol;
                        trade.price = tradePrice;
                        trade.quantity = tradeQuantity;
                        trade.timestamp = getTimestamp();
                        
                        pthread_mutex_lock(&tradeMutex);
                        trades.push_back(trade);
                        pthread_mutex_unlock(&tradeMutex);
                        
                        order.remainingQuantity -= tradeQuantity;
                        buyOrder.remainingQuantity -= tradeQuantity;
                        
                        pthread_mutex_lock(&clientSocketMutex);
                        
                        if (clientSockets.find(buyOrder.clientId) != clientSockets.end()) {
                            stringstream buyMsg;
                            buyMsg << "TRADE|" << trade.tradeId << "|ALIM|" 
                                  << symbol << "|" << fixed << setprecision(2) << tradePrice 
                                  << "|" << tradeQuantity << "|" << buyOrder.orderId;
                            sendToClient(clientSockets[buyOrder.clientId], buyMsg.str());
                        }
                        
                        if (clientSockets.find(order.clientId) != clientSockets.end()) {
                            stringstream sellMsg;
                            sellMsg << "TRADE|" << trade.tradeId << "|SATIM|" 
                                   << symbol << "|" << fixed << setprecision(2) << tradePrice 
                                   << "|" << tradeQuantity << "|" << order.orderId;
                            sendToClient(clientSockets[order.clientId], sellMsg.str());
                        }
                        
                        pthread_mutex_unlock(&clientSocketMutex);
                        
                        cout << "[" << getTimestamp() << "] İŞLEM - " << symbol 
                             << " " << tradeQuantity << " adet @ " << fixed << setprecision(2) 
                             << tradePrice << " TL (Alıcı: Client#" << buyOrder.clientId 
                             << ", Satıcı: Client#" << order.clientId << ")" << endl;
                        
                        ofstream tradeFile("trades.log", ios::app);
                        if (tradeFile.is_open()) {
                            tradeFile << getDateStamp() << " " << getTimestamp() 
                                     << "|" << trade.tradeId << "|" << symbol 
                                     << "|" << tradePrice << "|" << tradeQuantity
                                     << "|Client#" << buyOrder.clientId << "|Client#" << order.clientId << endl;
                            tradeFile.close();
                        }
                        
                        if (buyOrder.remainingQuantity == 0) {
                            book.buyOrders.pop_front();
                        }
                    } else {
                        break;
                    }
                }
            }
            
            if (order.remainingQuantity > 0) {
                if (order.type == "AL") {
                    deque<Order>::iterator it = book.buyOrders.begin();
                    while (it != book.buyOrders.end() && it->price > order.price) {
                        ++it;
                    }
                    book.buyOrders.insert(it, order);
                } else {
                    deque<Order>::iterator it = book.sellOrders.begin();
                    while (it != book.sellOrders.end() && it->price < order.price) {
                        ++it;
                    }
                    book.sellOrders.insert(it, order);
                }
            }
            
            pthread_mutex_unlock(&orderBookMutex);
            saveOrderBook();
            
            string response = "ORDER_ACCEPTED|" + order.orderId + "\n";
            send(clientSocket, response.c_str(), response.length(), 0);
        } else {
            string response = "OK\n";
            send(clientSocket, response.c_str(), response.length(), 0);
        }
    }
    
    pthread_mutex_lock(&clientSocketMutex);
    clientSockets.erase(clientId);
    pthread_mutex_unlock(&clientSocketMutex);

    close(clientSocket);
    
    pthread_mutex_lock(&clientCountMutex);
    activeClients--;
    cout << "[" << getTimestamp() << "] Client #" << clientId 
         << " ayrıldı (Aktif: " << activeClients << ")" << endl;
    pthread_mutex_unlock(&clientCountMutex);
    
    delete clientData;
    return NULL;
}

void showHelp() {
    cout << "\n=== SERVER KOMUTLARI ===" << endl;
    cout << "  emirler  - Son emirleri göster" << endl;
    cout << "  ozet     - Günlük özet raporu" << endl;
    cout << "  aktif    - Aktif client sayısı" << endl;
    cout << "  temizle  - Ekranı temizle" << endl;
    cout << "  bekleyen - Order book durumu (alış/satış emirleri)" << endl;
    cout << "  islemler - Günün gerçekleşen işlemlerini göster" << endl;
    cout << "  cikis    - Server'ı kapat" << endl;
    cout << "========================" << endl;
}

void* commandHandler(void* arg) {
    string command;
    while (serverRunning) {
        getline(cin, command);
        
        if (command == "emirler") {
            displayServerOrders();
        } else if (command == "ozet") {
            displayDailySummary();
        } else if (command == "aktif") {
            pthread_mutex_lock(&clientCountMutex);
            cout << "\nAktif client sayısı: " << activeClients << endl;
            pthread_mutex_unlock(&clientCountMutex);
        } else if (command == "temizle") {
            system("clear");
        } else if (command == "bekleyen") {
            displayOrderBook();
        } else if (command == "islemler") {
            displayTradeSummary();
        } else if (command == "yardim") {
            showHelp();
        } else if (command == "cikis") {
            cout << "\nServer kapatılıyor..." << endl;
            saveOrderBook();
            serverRunning = false;
            close(globalServerSocket);
            exit(0);
        } else if (!command.empty()) {
            cout << "Bilinmeyen komut. 'yardim' yazarak komutları görebilirsiniz." << endl;
        }
    }
    return NULL;
}
int main() {
    ConfigReader config;
    config.load("config.ini");
    
    int port = config.getInt("server", "port", 5001);
    int maxClients = config.getInt("server", "max_clients", 10);
    
    int serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSocket < 0) {
        cerr << "Socket oluşturma hatası!" << endl;
        return 1;
    }
    
    globalServerSocket = serverSocket;
    
    int opt = 1;
    setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    struct sockaddr_in serverAddress;
    memset(&serverAddress, 0, sizeof(serverAddress));
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_addr.s_addr = INADDR_ANY;
    serverAddress.sin_port = htons(port);
    
    if (bind(serverSocket, (struct sockaddr*)&serverAddress, sizeof(serverAddress)) < 0) {
        cerr << "Port " << port << " kullanımda!" << endl;
        close(serverSocket);
        return 1;
    }
    
    if (listen(serverSocket, maxClients) < 0) {
        cerr << "Listen hatası!" << endl;
        close(serverSocket);
        return 1;
    }
    
    cout << "\n==== SERVER ====" << endl;
    cout << "Port: " << port << endl;
    cout << "Max Client: " << maxClients << endl;
    cout << "===================" << endl;
    cout << "\n'yardim' yazarak komutları görebilirsiniz.\n" << endl;
    
    loadOrderBook();

    pthread_t autoSaveThread;
    pthread_create(&autoSaveThread, NULL, autoSaveOrderBook, NULL);
    pthread_detach(autoSaveThread);

    pthread_t commandThread;
    pthread_create(&commandThread, NULL, commandHandler, NULL);
    pthread_detach(commandThread);
    
    int clientCounter = 1;
    
    while (serverRunning) {
        struct sockaddr_in clientAddress;
        socklen_t clientAddressLength = sizeof(clientAddress);
        
        int clientSocket = accept(serverSocket, 
                                 (struct sockaddr*)&clientAddress, 
                                 &clientAddressLength);
        
        if (clientSocket < 0) {
            if (!serverRunning) break;
            continue;
        }
        
        ClientData* clientData = new ClientData;
        clientData->socket = clientSocket;
        clientData->id = clientCounter++;
        
        pthread_t thread;
        pthread_create(&thread, NULL, handleClient, clientData);
        pthread_detach(thread);
    }
    
    close(serverSocket);
    return 0;
}