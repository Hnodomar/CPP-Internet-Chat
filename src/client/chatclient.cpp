#include "chatclient.hpp"

ChatClient::ChatClient(
    const tcp::resolver::results_type& endpoints, 
    boost::asio::io_context& io_context
    ): 
    socket_(io_context), io_context_(io_context) 
{
    initClient(endpoints);
}

void ChatClient::initClient(const tcp::resolver::results_type& endpoints) {
    boost::asio::async_connect(
        socket_,
        endpoints,
        [this](boost::system::error_code ec, tcp::endpoint) {
            if (!ec) readMsgHeader();
        }
    );
    startInputLoop();
}

void ChatClient::startInputLoop() {
    boost::thread t([this](){
        io_context_.run(); //run async recv in one thread
    });                    //get input from another
    char user_input[max_body_len + 1];
    while (std::cin.getline(user_input, max_body_len + 1)) {
        Message msg_to_send;
        msg_to_send.setBodyLen(std::strlen(user_input));
        std::memcpy(
            msg_to_send.getMessagePacketBody(), 
            user_input,
            msg_to_send.getMessagePacketBodyLen() 
        );
        msg_to_send.addHeader();
        addMsgToQueue(msg_to_send);
    }
    t.join();
}

void ChatClient::addMsgToQueue(const Message& msg) {
    boost::asio::post(io_context_, [this, msg]() {
        bool already_writing = !msg_queue_.empty();
        msg_queue_.push_back(msg);
        if (!already_writing) writeMsgToServer();
    });
}

void ChatClient::writeMsgToServer() {
    boost::asio::async_write(
        socket_,
        boost::asio::buffer(
            msg_queue_.front().getMessagePacket(),
            msg_queue_.front().getMsgPacketLen()
        ),
        [this](boost::system::error_code ec, std::size_t) {
            if (!ec) {
                msg_queue_.pop_front();
                if (!msg_queue_.empty()) writeMsgToServer();
            }
            else closeSocket();
        }
    );
}

void ChatClient::readMsgHeader() {
    boost::asio::async_read(
        socket_,
        boost::asio::buffer(
            temp_msg_.getMessagePacket(),
            header_len
        ),
        [this](boost::system::error_code ec, std::size_t) {
            if (!ec && temp_msg_.parseHeader())
                readMsgBody();
            else closeSocket(); 
        }
    );
}

void ChatClient::readMsgBody() {
    boost::asio::async_read(
        socket_,
        boost::asio::buffer(
            temp_msg_.getMessagePacketBody(),
            temp_msg_.getMessagePacketBodyLen()
        ),  
        [this](boost::system::error_code ec, std::size_t) {
            if (!ec) {
                std::cout.write(
                    reinterpret_cast<const char*>(
                        temp_msg_.getMessagePacketBody()
                    ),
                    temp_msg_.getMessagePacketBodyLen()
                );
                std::cout << "\n";
                readMsgHeader();
            }
            else closeSocket();
        }
    );
}

void ChatClient::closeSocket() {
    boost::asio::post(io_context_, [this]() {
        socket_.close();
    });
}