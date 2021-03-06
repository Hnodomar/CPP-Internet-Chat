#ifndef CHAT_CLIENT_HPP
#define CLIENT_HPP

#include <boost/asio.hpp>
#include <boost/thread/thread.hpp>
#include <iostream>
#include <sstream>
#include <string>
#include <tuple>
#include <deque>

#include "message.hpp"
#include "util.hpp"

using boost::asio::ip::tcp;
typedef std::tuple<std::string, std::string> CmdAndArg;

class ChatClient {
    public:
        ChatClient(const tcp::resolver::results_type& endpoints, boost::asio::io_context& io_context);
        void initClient(const tcp::resolver::results_type& endpoints);
    private:
        void startInputLoop();
        void constructMsg(char* user_input);
        void writeUsernameToMsgBody(Message& msg, std::string& username);
        void interpretCommand(const char* input);
        void setClientNick(std::string nick);
        void readMsgHeader();
        void readMsgBody();
        void addMsgToQueue(const Message& msg);
        void writeMsgToServer();
        void closeSocket();
        void sendMsgToServerNoQueue(
            const std::string& body,
            char type,
            const std::string& success_msg,
            const std::string& fail_msg
        );
        void askServerToJoinRoom(std::string& roomname);
        void askServerForRoomList();
        void askServerForUserList();
        void askServerToCreateRoom(std::string& roomname);
        CmdAndArg getCmdAndArg(const char* input);

        void handleChatMsg();
        void handleNickMsg();
        void handleJoinMsg();
        void handleRoomListMsg();
        void handleUserListMsg();
        void handleCreateRoomMsg();

        void formatAndOutputList(bool is_room_list);
        void outputMsgBody();

        tcp::socket socket_;
        std::string username_;
        std::string username_temp_;
        std::string room_name_;
        bool checking_username_ = false;
        bool in_chatroom_ = false;
        boost::asio::io_context& io_context_;
        Message temp_msg_;
        std::deque<Message> msg_queue_;
};

#endif
