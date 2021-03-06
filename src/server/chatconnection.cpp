#include "chatconnection.hpp"

ChatConnection::ChatConnection(
    tcp::socket socket, 
    chatrooms& chat_rooms, 
    Logger& logger,
    std::mutex& chatroom_set_mutex,
    std::string client_address
    ): 
    socket_(std::move(socket)), 
    chatrooms_set_(chat_rooms), 
    chatroom_set_mutex_(chatroom_set_mutex),
    logger_(logger),
    client_address_(std::move(client_address))
{}

void ChatConnection::init() {
    readMsgHeader();
}

void ChatConnection::readMsgHeader() {
    auto self(shared_from_this());
    boost::asio::async_read(
        socket_,
        boost::asio::buffer(temp_msg_.getMessagePacket(), header_len),
        [this, self](boost::system::error_code ec, std::size_t) {
            if (!ec && temp_msg_.parseHeader()) {
                readMsgBody();
            }
            else {
                if (chatroom_ != nullptr) 
                    chatroom_->leave(self);
            }
        }
    );
}

void ChatConnection::readMsgBody() {
    auto self(shared_from_this());
    boost::asio::async_read(
        socket_,
        boost::asio::buffer(
            temp_msg_.getMessagePacketBody(), 
            temp_msg_.getMessagePacketBodyLen()
        ),
        [this, self](boost::system::error_code ec, std::size_t) {
            if (!ec) {
                switch(temp_msg_.type()) {
                    case 'M':
                        handleChatMsg();
                        break;
                    case 'N':
                        handleNickMsg();
                        break;
                    case 'J':
                        handleJoinRoomMsg();
                        break;
                    case 'L':
                        handleListRoomsMsg();
                        break;
                    case 'U':
                        handleListUsersMsg();
                        break;
                    case 'C':
                        handleCreateRoomMsg();
                        break;
                    default:
                        break;
                }
            }
            else {
                if (chatroom_ != nullptr)
                    chatroom_->leave(self);
            }
        }
    );
}

void ChatConnection::sendMsgToClientNoQueue(
    const std::string& success_msg, 
    const std::string& fail_msg,
    const std::string& body,
    char type) {
    sendMsgToSocketNoQueue(
        body,
        type,
        [this, 
        s=std::move(success_msg), 
        f=std::move(fail_msg), 
        self=shared_from_this()](boost::system::error_code ec, std::size_t) {
            if (!ec) {
                self->logger_.write(s);
            }
            else {
                self->logger_.write(f + "\n[ ERROR: ] " + ec.message());
                if (chatroom_ != nullptr)
                    chatroom_->leave(self);
            }
            self->readMsgHeader();
        },
        socket_
    );
}

void ChatConnection::handleCreateRoomMsg() {
    std::string new_room_name(
        reinterpret_cast<char*>(
            temp_msg_.getMessagePacketBody()
        ),
        temp_msg_.getMessagePacketBodyLen()   
    );
    if (chatroomNameExists(new_room_name))
        sendMsgToClientNoQueue(
            "[ SERVER ]: Notified client that room cannot be created: room exists",
            "[ SERVER ]: Failed to notify client that room cannot be created due to existence",
            "N",
            'C'
        );
    else {
        std::lock_guard<std::mutex> lock(chatroom_set_mutex_);
        sendMsgToClientNoQueue(
            "[ SERVER ]: Notified client that room can be created",
            "[ SERVER ]: Failed to notify client that room can be created",
            "Y",
            'C'
        );
        getChatrooms().emplace(
            std::make_shared<ChatRoom>(
                new_room_name
            )
        );
    }
}

void ChatConnection::handleListUsersMsg() {
    sendMsgToClientNoQueue(
        "[ SERVER ]: Sent client user list",
        "[ SERVER ]: Failed to send client user list",
        getChatroomNicksList(),
        'U'
    );
}

void ChatConnection::handleListRoomsMsg() {
    sendMsgToClientNoQueue(
        "[ SERVER ]: Successfully sent client chatroom list",
        "[ SERVER ]: Failed to send client chatroom list",
        getChatroomNameList(),
        'L'
    );
}

void ChatConnection::handleChatMsg() {
    logger_.write(
        "[  USER  ] [" + chatroom_->getRoomName() + "]: " + 
        std::string(
            reinterpret_cast<char*>(
                temp_msg_.getMessagePacketBody()
            ),
            temp_msg_.getMessagePacketBodyLen()
        )
    );
    if (chatroom_ != nullptr)
        chatroom_->deliverMsgToUsers(temp_msg_);
    readMsgHeader();
}

void ChatConnection::handleNickMsg() {
    char nick_request[10] = "";
    memcpy(
        nick_request, 
        temp_msg_.getMessagePacketBody(), 
        temp_msg_.getMessagePacketBodyLen()
    );
    if (chatroom_ == nullptr || chatroom_->nickAvailable(nick_request)) {
        strncpy(nick, nick_request, 10);
        sendMsgToClientNoQueue(
            "[ SERVER ]: Notified client that name change successful",
            "[ SERVER ]: Failed to notify client that name change successful",
            "Y",
            'N'
        );
        if (chatroom_ != nullptr)
            chatroom_->deliverMsgToUsers(
                NickChange(nick, nick_request)
            );
    }
    else {
        sendMsgToClientNoQueue(
            "[ SERVER ]: Notified client that name change unsuccessful",
            "[ SERVER ]: Failed to notify client that name change unsuccessful",
            "N",
            'N'
        );
    }
}

void ChatConnection::handleJoinRoomMsg() {
    auto self = shared_from_this();
    std::string room_name(
        reinterpret_cast<char*>(
            temp_msg_.getMessagePacketBody()
        ),
        temp_msg_.getMessagePacketBodyLen()   
    );
    auto chatroom_itr = getChatroomItrFromName(room_name);
    auto chatroomExists = [&]{return chatroom_itr != chatrooms_set_.end();};
    if (chatroomExists()) {
        if ((*chatroom_itr)->nickAvailable(nick)) {
            if (chatroom_ != nullptr)
                chatroom_->leave(self);
            chatroom_ = (*chatroom_itr);
            sendMsgToClientNoQueue( //room exists nick not in use
                "[ SERVER ]: Notified client that room join successful",
                "[ SERVER ]: Failed to notify client that room join successful",
                "Y",
                'J'
            );
            (*chatroom_itr)->join(self);
        }
        else {
            sendMsgToClientNoQueue(  //room exists but nick in use
                "[ SERVER ]: Notified client that room join unsuccessful: name in use",
                "[ SERVER ]: Failed to notify client that room join unsuccessful: name in use",
                "U",
                'J'
            );
        }
    }
    else {
        sendMsgToClientNoQueue( //room doesnt exist
            "[ SERVER ]: Notified client that room join unsuccessful: room doesnt exist",
            "[ SERVER ]: Failed to notify client that room join unsuccessful: room doesnt exist",
            "N",
            'J'
        );
    }
}

std::string ChatConnection::getChatroomNameList() {
    std::lock_guard<std::mutex> lock(chatroom_set_mutex_);
    std::string list;
    for (const auto& chatroom : chatrooms_set_) {
        list += chatroom->getRoomName();
        list += ' ';
    }
    list.pop_back();
    return list;
}

std::string ChatConnection::getChatroomNicksList() const {
    auto self = shared_from_this();
    return std::move(chatroom_->getNicksList());
}

chatrooms::iterator ChatConnection::getChatroomItrFromName(std::string& name) {
    const std::lock_guard<std::mutex> lock(chatroom_set_mutex_);
    return std::find_if(
        chatrooms_set_.begin(),
        chatrooms_set_.end(),
        [&name](std::shared_ptr<ChatRoom> chatroom){
            return name == chatroom->getRoomName();
        }
    );
}

bool ChatConnection::chatroomNameExists(std::string& name) {
    return !(getChatroomItrFromName(name) == chatrooms_set_.end());
}

void ChatConnection::writeMsgToClient(const Message& msg) {
    auto self(shared_from_this());
    Message temp = msg;
    boost::asio::async_write( //thread-safe: https://stackoverflow.com/questions/7362894/boostasiosocket-thread-safety
        socket_,
        boost::asio::buffer(
            temp.getMessagePacket(),
            temp.getMsgPacketLen()
        ),
        [this, self](boost::system::error_code ec, std::size_t) {
            if (ec)
                if (chatroom_ != nullptr)
                    chatroom_->leave(self);
        }
    );
}
