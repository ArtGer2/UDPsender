#include <iostream>
#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <complex>
#include <vector>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>

const size_t BUFFER_SIZE = 1024; 
const char* UDP_IP = "127.0.0.1"; 
const int UDP_PORT = 8080;        

// Функция получения данных от DMA
std::complex<int16_t>* GetDmaBuff(size_t N) {
    // Пример реализации для симуляции получения данных
    static std::vector<std::complex<int16_t>> buffer(N);
    return buffer.data();
}

// Потокобезопасная очередь
template<typename T>
class ThreadSafeQueue {
public:
    void push(T value) {
        std::lock_guard<std::mutex> lock(mtx);
        queue.push(std::move(value));
        cv.notify_one();
    }

    bool pop(T& result) {
        std::unique_lock<std::mutex> lock(mtx);
        cv.wait(lock, [this]() { return !queue.empty() || stop; });
        if (queue.empty()) return false;
        result = std::move(queue.front());
        queue.pop();
        return true;
    }

    void stopQueue() {
        std::lock_guard<std::mutex> lock(mtx);
        stop = true;
        cv.notify_all();
    }

private:
    std::queue<T> queue;
    std::mutex mtx;
    std::condition_variable cv;
    bool stop = false;
};

// Поток получения данных с DMA
void dmaThread(ThreadSafeQueue<std::vector<std::complex<int16_t>>>& queue, std::atomic<bool>& running) {
    while (running) {
        std::complex<int16_t>* dmaBuffer = GetDmaBuff(BUFFER_SIZE);
        std::vector<std::complex<int16_t>> data(dmaBuffer, dmaBuffer + BUFFER_SIZE);
        queue.push(std::move(data));
    }
}

// Поток отправки данных по UDP
void udpThread(ThreadSafeQueue<std::vector<std::complex<int16_t>>>& queue, std::atomic<bool>& running) {
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        std::cerr << "Ошибка создания сокета" << std::endl;
        return;
    }

    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(UDP_PORT);
    inet_pton(AF_INET, UDP_IP, &serverAddr.sin_addr);

    while (running) {
        std::vector<std::complex<int16_t>> data;
        if (queue.pop(data)) {
            sendto(sockfd, data.data(), data.size() * sizeof(std::complex<int16_t>), 0,
                   reinterpret_cast<sockaddr*>(&serverAddr), sizeof(serverAddr));
        }
    }

    close(sockfd);
}

int main() {
    ThreadSafeQueue<std::vector<std::complex<int16_t>>> queue;
    std::atomic<bool> running(true);

    std::thread dmaWorker(dmaThread, std::ref(queue), std::ref(running));
    std::thread udpWorker(udpThread, std::ref(queue), std::ref(running));

   
    std::cout << "Нажмите Enter для завершения программы..." << std::endl;
    std::cin.get();

    running = false;
    queue.stopQueue();

    dmaWorker.join();
    udpWorker.join();

    std::cout << "Программа завершена." << std::endl;
    return 0;
}
