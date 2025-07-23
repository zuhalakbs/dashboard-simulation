#ifndef ORDER_MANAGER_H
#define ORDER_MANAGER_H

#include <string>
#include <vector>
#include <fstream>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <iostream>

struct Order {
    std::string timestamp;
    std::string client_id;
    std::string symbol;
    std::string order_type;  
    double price;
    int quantity;
    double total_amount;
    std::string status;  
};

class OrderManager {
private:
    std::string ordersFile;
    std::string dailyFile;
    
    std::string getTimestamp() {
        time_t now = time(0);
        struct tm* timeinfo = localtime(&now);
        char buffer[30];
        strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", timeinfo);
        return std::string(buffer);
    }
    
    std::string getDateStamp() {
        time_t now = time(0);
        struct tm* timeinfo = localtime(&now);
        char buffer[20];
        strftime(buffer, sizeof(buffer), "%Y%m%d", timeinfo);
        return std::string(buffer);
    }

public:
    OrderManager() {
        ordersFile = "orders.log";
        dailyFile = "orders_" + getDateStamp() + ".csv";
    }
    
    bool saveOrder(const Order& order) {
        std::ofstream file(ordersFile, std::ios::app);
        if (!file.is_open()) {
            return false;
        }
        
        file << order.timestamp << "|"
             << order.client_id << "|"
             << order.symbol << "|"
             << order.order_type << "|"
             << std::fixed << std::setprecision(2) << order.price << "|"
             << order.quantity << "|"
             << std::fixed << std::setprecision(2) << order.total_amount << "|"
             << order.status << std::endl;
        
        file.close();
        
        saveDailyCSV(order);
        
        return true;
    }
    
    bool saveDailyCSV(const Order& order) {
        bool fileExists = std::ifstream(dailyFile).good();
        
        std::ofstream file(dailyFile, std::ios::app);
        if (!file.is_open()) {
            return false;
        }
        
        if (!fileExists) {
            file << "Zaman,Client ID,Hisse,İşlem,Fiyat,Miktar,Toplam,Durum" << std::endl;
        }
        
        file << order.timestamp << ","
             << order.client_id << ","
             << order.symbol << ","
             << order.order_type << ","
             << std::fixed << std::setprecision(2) << order.price << ","
             << order.quantity << ","
             << std::fixed << std::setprecision(2) << order.total_amount << ","
             << order.status << std::endl;
        
        file.close();
        return true;
    }
    
    bool saveOrderJSON(const Order& order) {
        std::string jsonFile = "orders_" + getDateStamp() + ".json";
        
        std::ifstream checkFile(jsonFile);
        bool fileExists = checkFile.good();
        checkFile.close();
        
        std::ofstream file;
        
        if (fileExists) {
            file.open(jsonFile, std::ios::in | std::ios::out);
            file.seekp(-2, std::ios::end);
            file << ",\n";
        } else {
            file.open(jsonFile);
            file << "{\n  \"orders\": [\n";
        }
        
        file << "    {\n"
             << "      \"timestamp\": \"" << order.timestamp << "\",\n"
             << "      \"client_id\": \"" << order.client_id << "\",\n"
             << "      \"symbol\": \"" << order.symbol << "\",\n"
             << "      \"order_type\": \"" << order.order_type << "\",\n"
             << "      \"price\": " << std::fixed << std::setprecision(2) << order.price << ",\n"
             << "      \"quantity\": " << order.quantity << ",\n"
             << "      \"total_amount\": " << std::fixed << std::setprecision(2) << order.total_amount << ",\n"
             << "      \"status\": \"" << order.status << "\"\n"
             << "    }";
        
        if (!fileExists) {
            file << "\n  ]\n}";
        } else {
            file << "\n  ]\n}";
        }
        
        file.close();
        return true;
    }
    
    Order createOrder(const std::string& client_id, const std::string& symbol,
                     const std::string& order_type, double price, int quantity,
                     const std::string& status = "EXECUTED") {
        Order order;
        order.timestamp = getTimestamp();
        order.client_id = client_id;
        order.symbol = symbol;
        order.order_type = order_type;
        order.price = price;
        order.quantity = quantity;
        order.total_amount = price * quantity;
        order.status = status;
        return order;
    }

    void generateDailySummary() {
        std::string summaryFile = "summary_" + getDateStamp() + ".txt";
        std::ofstream file(summaryFile);
        
        if (!file.is_open()) {
            return;
        }
        
        file << "=== GÜNLÜK EMİR ÖZETİ ===" << std::endl;
        file << "Tarih: " << getDateStamp() << std::endl;
        file << "========================" << std::endl;

        std::ifstream ordersIn(dailyFile);
        std::string line;
        int totalOrders = 0;
        int buyOrders = 0;
        int sellOrders = 0;
        double totalVolume = 0;
        

        std::getline(ordersIn, line);
        
        while (std::getline(ordersIn, line)) {
            std::stringstream ss(line);
            std::string item;
            std::vector<std::string> tokens;
            
            while (std::getline(ss, item, ',')) {
                tokens.push_back(item);
            }
            
            if (tokens.size() >= 7) {
                totalOrders++;
                if (tokens[3] == "BUY") buyOrders++;
                else if (tokens[3] == "SELL") sellOrders++;
                
                totalVolume += std::stod(tokens[6]); // Toplam tutar
            }
        }
        
        file << "Toplam Emir: " << totalOrders << std::endl;
        file << "Alış Emirleri: " << buyOrders << std::endl;
        file << "Satış Emirleri: " << sellOrders << std::endl;
        file << "Toplam İşlem Hacmi: " << std::fixed << std::setprecision(2) 
             << totalVolume << " TL" << std::endl;
        
        file.close();
        ordersIn.close();
        
        std::cout << "\nGünlük özet raporu oluşturuldu: " << summaryFile << std::endl;
    }
    
    void displayRecentOrders(int n = 10) {
        std::ifstream file(ordersFile);
        if (!file.is_open()) {
            std::cout << "Emir geçmişi bulunamadı." << std::endl;
            return;
        }
        
        std::vector<std::string> orders;
        std::string line;
        
        while (std::getline(file, line)) {
            orders.push_back(line);
        }
        file.close();
        
        std::cout << "\n=== SON " << n << " EMİR ===" << std::endl;
        std::cout << std::setw(20) << "Zaman" 
                  << std::setw(12) << "Client"
                  << std::setw(8) << "Hisse"
                  << std::setw(8) << "İşlem"
                  << std::setw(10) << "Fiyat"
                  << std::setw(8) << "Adet"
                  << std::setw(12) << "Toplam"
                  << std::setw(10) << "Durum" << std::endl;
        std::cout << std::string(90, '-') << std::endl;
        
        int start = std::max(0, (int)orders.size() - n);
        for (int i = start; i < orders.size(); i++) {
            std::stringstream ss(orders[i]);
            std::string item;
            std::vector<std::string> tokens;
            
            while (std::getline(ss, item, '|')) {
                tokens.push_back(item);
            }
            
            if (tokens.size() >= 8) {
                std::cout << std::setw(20) << tokens[0]
                         << std::setw(12) << tokens[1]
                         << std::setw(8) << tokens[2]
                         << std::setw(8) << tokens[3]
                         << std::setw(10) << tokens[4]
                         << std::setw(8) << tokens[5]
                         << std::setw(12) << tokens[6]
                         << std::setw(10) << tokens[7] << std::endl;
            }
        }
    }
};

#endif