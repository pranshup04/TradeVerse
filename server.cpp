#include <zmq.hpp>
#include <string>
#include <iostream>
#include <chrono>
#include <thread>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <mutex>
#include <vector>

// GLOBAL MATRIX: Shared across the data stream engine and all parallel chat workers
std::unordered_map<std::string, std::string> live_market_prices;
std::mutex market_lock; // Mutex to guarantee absolute thread safety during concurrent read/writes

// PARALLEL CHAT WORKER ROUTINE
void chatbox_worker_routine(zmq::context_t* context) {
    // Each worker thread connects internally to the proxy backend using an in-process channel
    zmq::socket_t worker(*context, zmq::socket_type::rep);
    worker.connect("inproc://backend");

    while (true) {
        zmq::message_t request;
        auto result = worker.recv(request, zmq::recv_flags::none);
        
        std::string client_msg(static_cast<char*>(request.data()), request.size());
        
        // POC TRACKING: Print the unique hardware thread ID handling this request instantly
        std::cout << "\n🧵 [Thread " << std::this_thread::get_id() << "] Picked up request: " << client_msg << std::endl;
        
        // POC ARTIFICIAL LAG: Simulate heavy lookup processing delay (3 seconds)
        // This lets you test and prove parallel handling by hitting it from multiple terminals at once
        std::this_thread::sleep_for(std::chrono::seconds(3)); 

        std::string reply_msg;

        // Dynamic Parsing: Look up stock prices
        if (client_msg.rfind("FETCH:", 0) == 0) {
            std::string target_ticker = client_msg.substr(6); 
            
            std::lock_guard<std::mutex> lock(market_lock);
            if (live_market_prices.count(target_ticker)) {
                reply_msg = "SUCCESS | Asset: " + target_ticker + " | Latest Price: " + live_market_prices[target_ticker];
            } else {
                reply_msg = "ERROR | Asset '" + target_ticker + "' is not active in the tracking matrix.";
            }
        } 
        // Baseline Control Commands
        else if (client_msg == "STATUS_CHECK") {
            reply_msg = "HEALTH: Thread-pool processing framework active and online.";
        } else if (client_msg == "MANUAL_OVERRIDE") {
            reply_msg = "SUCCESS: Emergency command protocols initialized.";
        } else {
            reply_msg = "ACK: Query processed by assigned worker thread.";
        }

        // Send the response back through the proxy framework to the exact requesting client
        zmq::message_t reply(reply_msg.size());
        memcpy(reply.data(), reply_msg.data(), reply_msg.size());
        worker.send(reply, zmq::send_flags::none);
        
        std::cout << "✅ [Thread " << std::this_thread::get_id() << "] Processing complete. Response sent." << std::endl;
    }
}

// ROUTER-DEALER TRAFFIC MANAGER
void run_chatbox_proxy_server() {
    zmq::context_t context(1);

    // Frontend socket handles the external connections from Python clients on Port 5556
    zmq::socket_t frontend(context, zmq::socket_type::router);
    frontend.bind("tcp://*:5556");

    // Backend socket distributes the traffic internally to our thread pool queue
    zmq::socket_t backend(context, zmq::socket_type::dealer);
    backend.bind("inproc://backend");

    // Launch a thread pool consisting of 10 parallel background workers
    std::vector<std::thread> worker_threads;
    for (int i = 0; i < 10; ++i) {
        worker_threads.push_back(std::thread(chatbox_worker_routine, &context));
    }

    std::cout << "[INIT] Multithreaded Control Plane active with 10 workers on port 5556." << std::endl;

    // Turn on the automatic load-balancing proxy engine
    zmq::proxy(frontend, backend);

    // Safety joining logic if proxy context ever breaks down
    for (auto& t : worker_threads) {
        if (t.joinable()) t.join();
    }
}

// MAIN EXECUTION THREAD (DATA PLANE ENGINE)
int main() {
    // 1. Fire up the multithreaded command control proxy subsystem
    std::thread proxy_worker(run_chatbox_proxy_server);
    proxy_worker.detach(); 

    // 2. Setup the high-speed Data Broadcast socket on Port 5555
    zmq::context_t context(1);
    zmq::socket_t publisher(context, zmq::socket_type::pub);
    publisher.bind("tcp://*:5555"); 
    
    std::cout << "[INIT] Exchange Stream engine active on port 5555." << std::endl;

    std::ifstream file("market_data1.csv");
    if (!file.is_open()) {
        std::cerr << "CRITICAL ERROR: Could not open market_data1.csv file. Verify location." << std::endl;
        return 1;
    }

    std::string line;
    std::getline(file, line); // Skip the initial CSV column header row

    while (std::getline(file, line)) {
        std::stringstream ss(line);
        std::string timestamp, ticker, price;

        std::getline(ss, timestamp, ',');
        std::getline(ss, ticker, ',');
        std::getline(ss, price, ',');

        std::string message_string = ticker + "," + timestamp + "," + price;

        // Update the central map memory safe directory before blasting out the network message
        {
            std::lock_guard<std::mutex> lock(market_lock);
            live_market_prices[ticker] = "INR " + price; 
        }

        // Broadcast out to any live dashboard listener connected to Port 5555
        zmq::message_t message(message_string.size());
        memcpy(message.data(), message_string.data(), message_string.size());
        publisher.send(message, zmq::send_flags::none);
        
        std::cout << "Broadcasted Data: " << message_string << std::endl;
        std::this_thread::sleep_for(std::chrono::milliseconds(500)); // Smooth data pacing speed
    }
    
    file.close();
    return 0;
}