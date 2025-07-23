#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <errno.h>
#include <string>
#include <iomanip>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <limits>
#include <cmath>
#include <sys/time.h>
#include "config_reader.h"
#include "json_parser.h"
#include "order_manager.h"

using namespace std;

class StockClient {
private:
    int clientSocket;
    string clientId;
    vector<Stock> stocks;
    StockConfigParser parser;
    OrderManager orderManager;
    
    string toUpper(string str) {
        transform(str.begin(), str.end(), str.begin(), ::toupper);
        return str;
    }
    
    void clearInputBuffer() {
        cin.clear();
        cin.ignore(numeric_limits<streamsize>::max(), '\n');
    }
    
    bool isConnected() {
        if (clientSocket < 0) return false;
        
        int error = 0;
        socklen_t len = sizeof(error);
        int retval = getsockopt(clientSocket, SOL_SOCKET, SO_ERROR, &error, &len);
        
        if (retval != 0 || error != 0) {
            return false;
        }
        
        return true;
    }
    
    void displayStocks() {
        cout << "\n=== HISSE LISTESI ===" << endl;
        cout << setw(5) << "No" << setw(10) << "Sembol" << setw(25) << "İsim" 
             << setw(12) << "Fiyat" << setw(12) << "Tick" << endl;
        cout << string(70, '-') << endl;
        
        for (size_t i = 0; i < stocks.size(); i++) {
            cout << setw(5) << (i + 1) 
                 << setw(10) << stocks[i].symbol
                 << setw(25) << stocks[i].name
                 << setw(12) << fixed << setprecision(2) << stocks[i].base_price
                 << setw(12) << fixed << setprecision(2) << stocks[i].tick_size 
                 << endl;
        }
        cout << string(70, '-') << endl;
    }
    
    void placeOrder() {
        displayStocks();

        int choice;
        cout << "\nHisse seçin (1-" << stocks.size() << ") veya 0 ile iptal: ";
        
        if (!(cin >> choice)) {
            cout << "Geçersiz giriş!" << endl;
            clearInputBuffer();
            return;
        }
        
        if (choice == 0) {
            clearInputBuffer();
            return;
        }
        
        if (choice < 1 || choice > stocks.size()) {
            cout << "Geçersiz seçim!" << endl;
            clearInputBuffer();
            return;
        }
        
        Stock& selectedStock = stocks[choice - 1];
        cout << "\nSeçilen: " << selectedStock.symbol << " - " << selectedStock.name << endl;
        cout << "Güncel fiyat: " << selectedStock.base_price << " TL" << endl;
        cout << "Tick size: " << selectedStock.tick_size << " TL" << endl;

        string orderType;
        bool validOrderType = false;
        
        while (!validOrderType) {
            cout << "\nEmir tipi (AL/SAT) veya iptal için 'C': ";
            cin >> orderType;
            orderType = toUpper(orderType);
            
            if (orderType == "C") {
                clearInputBuffer();
                return;
            }
            
            if (orderType == "AL" || orderType == "SAT") {
                validOrderType = true;
            } else {
                cout << "Geçersiz emir tipi! Sadece AL veya SAT giriniz." << endl;
            }
        }

        double price;
        bool validPrice = false;
        
        while (!validPrice) {
            cout << "Fiyat (0 ile iptal): ";
            
            if (!(cin >> price)) {
                cout << "Geçersiz fiyat!" << endl;
                clearInputBuffer();
                continue;
            }
            
            if (price == 0) {
                clearInputBuffer();
                return;
            }
            
            if (price < 0) {
                cout << "Fiyat negatif olamaz!" << endl;
                continue;
            }
            
            double priceDiff = price - selectedStock.base_price;
            double tickCount = priceDiff / selectedStock.tick_size;
            
            if (abs(tickCount - round(tickCount)) > 0.001) {
                cout << "HATA: Fiyat tick size (" << selectedStock.tick_size 
                     << " TL) ile uyumlu değil!" << endl;
                
                double lowerPrice = selectedStock.base_price + (floor(tickCount) * selectedStock.tick_size);
                double upperPrice = selectedStock.base_price + (ceil(tickCount) * selectedStock.tick_size);
                
                cout << "En yakın geçerli fiyatlar: " 
                     << fixed << setprecision(2) << lowerPrice << " TL veya " 
                     << upperPrice << " TL" << endl;
                
                continue;
            }
            
            validPrice = true;
        }
        
        int quantity;
        bool validQuantity = false;
        
        while (!validQuantity) {
            cout << "Miktar (0 ile iptal): ";
            
            if (!(cin >> quantity)) {
                cout << "Geçersiz miktar!" << endl;
                clearInputBuffer();
                continue;
            }
            
            if (quantity == 0) {
                clearInputBuffer();
                return;
            }
            
            if (quantity < 0) {
                cout << "Miktar negatif olamaz!" << endl;
                continue;
            }
            
            if (quantity > 1000000) {
                cout << "Miktar çok yüksek! (Max: 1.000.000)" << endl;
                continue;
            }
            
            validQuantity = true;
        }
        
        Order order = orderManager.createOrder(clientId, selectedStock.symbol, 
                                             orderType, price, quantity, "EXECUTED");
        
        if (orderManager.saveOrder(order)) {
            cout << "\n=== EMİR ÖZETİ ===" << endl;
            cout << "Hisse: " << selectedStock.symbol << endl;
            cout << "İşlem: " << orderType << endl;
            cout << "Fiyat: " << fixed << setprecision(2) << price << " TL" << endl;
            cout << "Miktar: " << quantity << " adet" << endl;
            cout << "Toplam: " << fixed << setprecision(2) << (price * quantity) << " TL" << endl;
            cout << "\nEmir server'a gönderiliyor..." << endl;
            
            stringstream orderMsg;
            orderMsg << "EMIR|" << selectedStock.symbol << "|" << orderType 
                    << "|" << price << "|" << quantity;
            
            string message = orderMsg.str();
            ssize_t bytesSent = send(clientSocket, message.c_str(), message.length(), 0);
            
            if (bytesSent <= 0) {
                cout << "HATA: Server bağlantısı koptu! Emir gönderilemedi." << endl;
                cout << "Lütfen programı yeniden başlatın." << endl;
                clearInputBuffer();
                return;
            }
            
            cout << "Emir gönderildi, server yanıtı bekleniyor..." << endl;
            

            char buffer[1024];
            memset(buffer, 0, sizeof(buffer));
            

            struct timeval timeout;
            timeout.tv_sec = 5; 
            timeout.tv_usec = 0;
            setsockopt(clientSocket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
            
            ssize_t bytesReceived = recv(clientSocket, buffer, sizeof(buffer) - 1, 0);
            
            if (bytesReceived > 0) {
                buffer[bytesReceived] = '\0';
                string response(buffer);
                
                if (response.find("ORDER_ACCEPTED") != string::npos) {
                    cout << "✓ Emir başarıyla server'a iletildi!" << endl;
                    
                    size_t pipePos = response.find('|');
                    if (pipePos != string::npos && pipePos + 1 < response.length()) {
                        string orderId = response.substr(pipePos + 1);
                        orderId.erase(remove(orderId.begin(), orderId.end(), '\n'), orderId.end());
                        orderId.erase(remove(orderId.begin(), orderId.end(), '\r'), orderId.end());
                        cout << "Emir ID: " << orderId << endl;
                    }
                } else {
                    cout << "Server yanıtı: " << response << endl;
                }
            } else if (bytesReceived == 0) {
                cout << "HATA: Server bağlantısı kapandı!" << endl;
                cout << "Lütfen programı yeniden başlatın." << endl;
            } else {
                cout << "HATA: Server yanıtı alınamadı (timeout veya bağlantı sorunu)" << endl;
                cout << "Emir server'a ulaşmış olabilir, 'bekleyen' komutu ile kontrol edin." << endl;
            }
            
            timeout.tv_sec = 0;
            timeout.tv_usec = 0;
            setsockopt(clientSocket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
            
        } else {
            cout << "Emir kayıt hatası!" << endl;
        }
        
        clearInputBuffer();
    }

public:
    StockClient(const string& id) : clientId(id), clientSocket(-1) {}
    
    bool loadStocks(const string& filename) {
        stocks = parser.loadStocks(filename);
        return !stocks.empty();
    }
    
    bool connect(const string& serverIp, int serverPort) {
        clientSocket = socket(AF_INET, SOCK_STREAM, 0);
        if (clientSocket < 0) {
            return false;
        }
        
        struct sockaddr_in serverAddress;
        memset(&serverAddress, 0, sizeof(serverAddress));
        serverAddress.sin_family = AF_INET;
        serverAddress.sin_port = htons(serverPort);
        serverAddress.sin_addr.s_addr = inet_addr(serverIp.c_str());
        
        if (::connect(clientSocket, (struct sockaddr*)&serverAddress, sizeof(serverAddress)) < 0) {
            close(clientSocket);
            return false;
        }
        
        return true;
    }
    
    void run() {
        cout << "\n=== BORSA EMİR SİSTEMİ ===" << endl;
        cout << "Client ID: " << clientId << endl;
        

        char buffer[1024];
        ssize_t welcomeBytes = recv(clientSocket, buffer, sizeof(buffer) - 1, 0);
        
        if (welcomeBytes <= 0) {
            cout << "Server bağlantı sorunu!" << endl;
            return;
        }
        
        buffer[welcomeBytes] = '\0';
        cout << "Server: " << buffer << endl;
        
        string command;
        while (true) {
            if (!isConnected()) {
                cout << "\n*** UYARI: Server bağlantısı kopmuş! ***" << endl;
                cout << "Lütfen programı yeniden başlatın." << endl;
                break;
            }
            
            cout << "\n===== MENÜ =====" << endl;
            cout << "[1] Hisse Listesi" << endl;
            cout << "[2] Emir Ver" << endl;
            cout << "[3] Çıkış" << endl;
            cout << "Seçim: ";
            
            getline(cin, command);
            
            if (command.empty()) {
                continue;
            }
            
            if (command == "1") {
                displayStocks();
            } else if (command == "2") {
                if (!isConnected()) {
                    cout << "HATA: Server bağlantısı kopuk! Emir verilemez." << endl;
                    continue;
                }
                placeOrder();
            } else if (command == "3" || toUpper(command) == "EXIT" || toUpper(command) == "QUIT") {
                cout << "Çıkış yapılıyor..." << endl;
                
                if (isConnected()) {
                    send(clientSocket, "quit", 4, 0);
                    
                    struct timeval timeout;
                    timeout.tv_sec = 2;
                    timeout.tv_usec = 0;
                    setsockopt(clientSocket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
                    
                    memset(buffer, 0, sizeof(buffer));
                    recv(clientSocket, buffer, sizeof(buffer) - 1, 0);
                }
                break;
            } else {
                cout << "Geçersiz seçim! Lütfen 1, 2 veya 3 giriniz." << endl;
            }
        }
        
        if (clientSocket >= 0) {
            close(clientSocket);
        }
    }
};

int main() {
    ConfigReader config;
    config.load("config.ini");
    
    string serverIp = config.get("client", "server_ip", "127.0.0.1");
    int serverPort = config.getInt("client", "server_port", 5001);
    
    string clientId = "CLIENT_" + to_string(time(0) % 10000);
    
    StockClient client(clientId);
    
    if (!client.loadStocks("stocks_config.json")) {
        cout << "Hisse listesi yüklenemedi!" << endl;
        return 1;
    }
    
    if (!client.connect(serverIp, serverPort)) {
        cout << "Server bağlantısı başarısız!" << endl;
        return 1;
    }
    
    client.run();
    
    cout << "Program sonlandı." << endl;
    
    return 0;
}