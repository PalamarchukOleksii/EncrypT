//
// chat_server.cpp
// ~~~~~~~~~~~~~~~
//
// Copyright (c) 2003-2023 Christopher M. Kohlhoff (chris at kohlhoff dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <cstdlib>
#include <deque>
#include <iostream>
#include <list>
#include <memory>
#include <set>
#include <utility>
#include "asio.hpp"
#include "ChatMessage.hpp"
#include "ChatServer.hpp"

using asio::ip::tcp;

typedef std::deque<chat_message> chat_message_queue;

typedef std::shared_ptr<chat_participant> chat_participant_ptr;

void ChatRoom::join(chat_participant_ptr participant)
{
  participants_.insert(participant);
  for (auto msg : recent_msgs_)
    participant->deliver(msg);
}

void ChatRoom::leave(chat_participant_ptr participant)
{
  participants_.erase(participant);
}

void ChatRoom::deliver(const chat_message &msg)
{
  recent_msgs_.push_back(msg);
  while (recent_msgs_.size() > max_recent_msgs)
    recent_msgs_.pop_front();

  for (auto participant : participants_)
    participant->deliver(msg);
}

ChatSession::ChatSession(tcp::socket socket, chat_room &room)
    : socket_(std::move(socket)),
      room_(room)
{
}

void ChatSession::start()
{
  room_.join(shared_from_this());
  do_read_header();
}

void ChatSession::deliver(const chat_message &msg)
{
  bool write_in_progress = !write_msgs_.empty();
  write_msgs_.push_back(msg);
  if (!write_in_progress)
  {
    do_write();
  }
}

void ChatSession::do_read_header()
{
  auto self(shared_from_this());
  asio::async_read(socket_,
                   asio::buffer(read_msg_.data(), chat_message::header_length),
                   [this, self](std::error_code ec, std::size_t /*length*/)
                   {
                     if (!ec && read_msg_.decode_header())
                     {
                       do_read_body();
                     }
                     else
                     {
                       room_.leave(shared_from_this());
                     }
                   });
}

void ChatSession::do_read_body()
{
  auto self(shared_from_this());
  asio::async_read(socket_,
                   asio::buffer(read_msg_.body(), read_msg_.body_length()),
                   [this, self](std::error_code ec, std::size_t /*length*/)
                   {
                     if (!ec)
                     {
                       room_.deliver(read_msg_);
                       do_read_header();
                     }
                     else
                     {
                       room_.leave(shared_from_this());
                     }
                   });
}

void ChatSession::do_write()
{
  auto self(shared_from_this());
  asio::async_write(socket_,
                    asio::buffer(write_msgs_.front().data(),
                                 write_msgs_.front().length()),
                    [this, self](std::error_code ec, std::size_t /*length*/)
                    {
                      if (!ec)
                      {
                        write_msgs_.pop_front();
                        if (!write_msgs_.empty())
                        {
                          do_write();
                        }
                      }
                      else
                      {
                        room_.leave(shared_from_this());
                      }
                    });
}

ChatServer::ChatServer(asio::io_context &io_context,
                       const tcp::endpoint &endpoint)
    : acceptor_(io_context, endpoint)
{
  do_accept();
}

void ChatServer::do_accept()
{
  acceptor_.async_accept(
      [this](std::error_code ec, tcp::socket socket)
      {
        if (!ec)
        {
          std::make_shared<chat_session>(std::move(socket), room_)->start();
        }

        do_accept();
      });
}

int main(int argc, char *argv[])
{
  try
  {
    if (argc < 2)
    {
      std::cerr << "Usage: chat_server <port> [<port> ...]\n";
      return 1;
    }

    asio::io_context io_context;

    std::list<chat_server> servers;
    for (int i = 1; i < argc; ++i)
    {
      tcp::endpoint endpoint(tcp::v4(), std::atoi(argv[i]));
      servers.emplace_back(io_context, endpoint);
    }

    io_context.run();
  }
  catch (std::exception &e)
  {
    std::cerr << "Exception: " << e.what() << "\n";
  }

  return 0;
}