#include <functional>
#include <optional>

#include "botcraft/Network/NetworkManager.hpp"
#include "botcraft/Network/TCP_Com.hpp"
#include "botcraft/Network/Authentifier.hpp"
#include "botcraft/Network/AESEncrypter.hpp"
#if USE_COMPRESSION
#include "botcraft/Network/Compression.hpp"
#endif
#include "botcraft/Utilities/Logger.hpp"
#include "botcraft/Utilities/SleepUtilities.hpp"
#if PROTOCOL_VERSION > 758 /* > 1.18.2 */
#include "botcraft/Utilities/StringUtilities.hpp"
#endif


#include "protocolCraft/BinaryReadWrite.hpp"
#include "protocolCraft/MessageFactory.hpp"

using namespace ProtocolCraft;

namespace Botcraft
{
    NetworkManager::NetworkManager(const std::string& address, const std::string& login, const bool force_microsoft_auth, const std::vector<Handler*>& handlers)
    {
        com = nullptr;

        // Online mode with Microsoft login flow
        if (login.empty() || force_microsoft_auth)
        {
            authentifier = std::make_shared<Authentifier>();
            if (!authentifier->AuthMicrosoft(login))
            {
                throw std::runtime_error("Error trying to authenticate on Mojang server using Microsoft auth flow");
            }
            name = authentifier->GetPlayerDisplayName();
        }
        // Online mode false
        else
        {
            authentifier = nullptr;
            name = login;
        }

        compression = -1;
        AddHandler(this);
        for (Handler* p : handlers)
        {
            AddHandler(p);
        }

        state = ConnectionState::Handshake;

        //Start the thread to process the incoming packets
        m_thread_process = std::thread(&NetworkManager::WaitForNewPackets, this);

        com = std::make_shared<TCP_Com>(address, std::bind(&NetworkManager::OnNewRawData, this, std::placeholders::_1));

        // Wait for the communication to be ready before sending any data
        Utilities::WaitForCondition([&]() {
            return com->IsInitialized();
        }, 500, 0);

        std::shared_ptr<ServerboundClientIntentionPacket> handshake_msg = std::make_shared<ServerboundClientIntentionPacket>();
        handshake_msg->SetProtocolVersion(PROTOCOL_VERSION);
        handshake_msg->SetHostName(com->GetIp());
        handshake_msg->SetPort(com->GetPort());
        handshake_msg->SetIntention(static_cast<int>(ConnectionState::Login));
        Send(handshake_msg);

        state = ConnectionState::Login;

        std::shared_ptr<ServerboundHelloPacket> loginstart_msg = std::make_shared<ServerboundHelloPacket>();
#if PROTOCOL_VERSION < 759 /* < 1.19 */
        loginstart_msg->SetGameProfile(name);
#else
        loginstart_msg->SetName_(name);
        if (authentifier)
        {
#if PROTOCOL_VERSION < 761 /* < 1.19.3 */
            ProfilePublicKey key;
            key.SetTimestamp(authentifier->GetKeyTimestamp());
            key.SetKey(Utilities::RSAToBytes(authentifier->GetPublicKey()));
            key.SetSignature(Utilities::DecodeBase64(authentifier->GetKeySignature()));

            loginstart_msg->SetPublicKey(key);
#else
            message_sent_index = 0;
#endif
#if PROTOCOL_VERSION > 759 /* > 1.19 */
            loginstart_msg->SetProfileId(authentifier->GetPlayerUUID());
#endif
        }
#endif
        Send(loginstart_msg);
    }

    NetworkManager::NetworkManager(const ConnectionState constant_connection_state)
    {
        state = constant_connection_state;
        compression = -1;
    }

    NetworkManager::~NetworkManager()
    {
        Stop();
    }

    void NetworkManager::Stop()
    {
        state = ConnectionState::None;

        if (com)
        {
            com->close();
        }

        process_condition.notify_all();

        if (m_thread_process.joinable())
        {
            m_thread_process.join();
        }
        compression = -1;

        com.reset();
    }

    void NetworkManager::AddHandler(Handler* h)
    {
        subscribed.push_back(h);
    }

    void NetworkManager::Send(const std::shared_ptr<Message> msg)
    {
        if (com)
        {
            std::lock_guard<std::mutex> lock(mutex_send);
            std::vector<unsigned char> msg_data;
            msg_data.reserve(256);
            msg->Write(msg_data);
            if (compression == -1)
            {
                com->SendPacket(msg_data);
            }
            else
            {
#ifdef USE_COMPRESSION
                if (msg_data.size() < compression)
                {
                    msg_data.insert(msg_data.begin(), 0x00);
                    com->SendPacket(msg_data);
                }
                else
                {
                    std::vector<unsigned char> compressed_msg;
                    compressed_msg.reserve(msg_data.size() + 5);
                    WriteData<VarInt>(static_cast<int>(msg_data.size()), compressed_msg);
                    std::vector<unsigned char> compressed_data = Compress(msg_data);
                    compressed_msg.insert(compressed_msg.end(), compressed_data.begin(), compressed_data.end());
                    com->SendPacket(compressed_msg);
                }
#else
                throw std::runtime_error("Program compiled without ZLIB. Cannot send compressed message");
#endif
            }
        }
    }

    const ConnectionState NetworkManager::GetConnectionState() const
    {
        return state;
    }

    const std::string& NetworkManager::GetMyName() const
    {
        return name;
    }

    void NetworkManager::SendChatMessage(const std::string& message)
    {
#if PROTOCOL_VERSION > 758 /* > 1.18.2 */
        if (message[0] == '/')
        {
            LOG_INFO("You're trying to send a message starting with '/'. Use SendChatCommand instead if you want the server to interprete it as a command.");
        }
#endif

        std::shared_ptr<ServerboundChatPacket> chat_message = std::make_shared<ServerboundChatPacket>();
        chat_message->SetMessage(message);
#if PROTOCOL_VERSION > 758 /* > 1.18.2 */
#if PROTOCOL_VERSION < 761 /* < 1.19.3 */
        chat_message->SetSignedPreview(false);
#endif
        if (authentifier)
        {
            long long int salt, timestamp;
            std::vector<unsigned char> signature;
#if PROTOCOL_VERSION == 759 /* 1.19 */
            // 1.19
            signature = authentifier->GetMessageSignature(message, salt, timestamp);
#elif PROTOCOL_VERSION == 760 /* 1.19.1/2 */
            // 1.19.1 and 1.19.2
            LastSeenMessagesUpdate last_seen_update;
            {
                std::scoped_lock<std::mutex> lock_messages(chat_context.GetMutex());
                last_seen_update = chat_context.GetLastSeenMessagesUpdate();
                signature = authentifier->GetMessageSignature(message, chat_context.GetLastSignature(), last_seen_update.GetLastSeen(), salt, timestamp);
                // Update last signature with current one for the next message
                chat_context.SetLastSignature(signature);
            }
            chat_message->SetLastSeenMessages(last_seen_update);
#else
            // 1.19.3+
            const auto [signatures, updates] = chat_context.GetLastSeenMessagesUpdate();
            const int current_message_sent_index = message_sent_index++;
            signature = authentifier->GetMessageSignature(message, current_message_sent_index, chat_session_uuid, signatures, salt, timestamp);
            chat_message->SetLastSeenMessages(updates);
#endif
            if (signature.empty())
            {
                throw std::runtime_error("Empty chat message signature.");
            }
            chat_message->SetTimestamp(timestamp);

#if PROTOCOL_VERSION < 760 /* < 1.19.1 */
            SaltSignature salt_signature;
            salt_signature.SetSalt(salt);
            salt_signature.SetSignature(signature);

            chat_message->SetSaltSignature(salt_signature);
#else
            chat_message->SetSalt(salt);
            chat_message->SetSignature(signature);
#endif
        }
        // Offline mode, empty signature
        else
        {
            chat_message->SetTimestamp(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count());
        }
#endif
        Send(chat_message);
    }

    void NetworkManager::SendChatCommand(const std::string& command)
    {
#if PROTOCOL_VERSION > 765 /* > 1.20.4 */
        if (authentifier == nullptr)
        {
            std::shared_ptr<ServerboundChatCommandPacket> chat_command = std::make_shared<ServerboundChatCommandPacket>();
            chat_command->SetCommand(command);
            Send(chat_command);
            return;
        }
#endif
#if PROTOCOL_VERSION > 758 /* > 1.18.2 */
#if PROTOCOL_VERSION > 765 /* > 1.20.4 */
        std::shared_ptr<ServerboundChatCommandSignedPacket> chat_command = std::make_shared<ServerboundChatCommandSignedPacket>();
#else
        std::shared_ptr<ServerboundChatCommandPacket> chat_command = std::make_shared<ServerboundChatCommandPacket>();
#endif
        chat_command->SetCommand(command);
        chat_command->SetTimestamp(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count());
#if PROTOCOL_VERSION > 759 /* > 1.19 */
        std::mt19937 rnd(static_cast<unsigned int>(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::high_resolution_clock::now().time_since_epoch()).count()));
        chat_command->SetSalt(std::uniform_int_distribution<long long int>(std::numeric_limits<long long int>::min(), std::numeric_limits<long long int>::max())(rnd));
#endif
#if PROTOCOL_VERSION < 761 /* < 1.19.3 */
        chat_command->SetSignedPreview(false);
#endif
#if PROTOCOL_VERSION == 760 /* 1.19.1/2 */
        LastSeenMessagesUpdate last_seen_update;
        if (authentifier)
        {
            std::scoped_lock<std::mutex> lock_messages(chat_context.GetMutex());
            last_seen_update = chat_context.GetLastSeenMessagesUpdate();
        }
        chat_command->SetLastSeenMessages(last_seen_update);
#elif PROTOCOL_VERSION > 760 /* > 1.19.2 */
        const auto [signatures, updates] = chat_context.GetLastSeenMessagesUpdate();
        chat_command->SetLastSeenMessages(updates);
#endif
        // TODO: when this shouldn't be empty? Can't find a situation where it's filled with something
        chat_command->SetArgumentSignatures({});
#else
        std::shared_ptr<ServerboundChatPacket> chat_command = std::make_shared<ServerboundChatPacket>();
        chat_command->SetMessage("/" + command);
#endif
        Send(chat_command);
    }

    std::thread::id NetworkManager::GetProcessingThreadId() const
    {
        return m_thread_process.get_id();
    }

    void NetworkManager::WaitForNewPackets()
    {
        Logger::GetInstance().RegisterThread("NetworkPacketProcessing - " + name);
        try
        {
            while (state != ConnectionState::None)
            {
                {
                    std::unique_lock<std::mutex> lck(mutex_process);
                    process_condition.wait(lck);
                }
                while (!packets_to_process.empty())
                {
                    std::vector<unsigned char> packet;
                    { // process_guard scope
                        std::lock_guard<std::mutex> process_guard(mutex_process);
                        if (!packets_to_process.empty())
                        {
                            packet = packets_to_process.front();
                            packets_to_process.pop();
                        }
                    }
                    if (packet.size() > 0)
                    {
                        if (compression == -1)
                        {
                            ProcessPacket(packet);
                        }
                        else
                        {
#ifdef USE_COMPRESSION
                            size_t length = packet.size();
                            ReadIterator iter = packet.begin();
                            int data_length = ReadData<VarInt>(iter, length);

                            //Packet not compressed
                            if (data_length == 0)
                            {
                                //Erase the first 0
                                packet.erase(packet.begin());
                                ProcessPacket(packet);
                            }
                            //Packet compressed
                            else
                            {
                                const int size_varint = static_cast<int>(packet.size() - length);

                                std::vector<unsigned char> uncompressed_msg = Decompress(packet, size_varint);
                                ProcessPacket(uncompressed_msg);
                            }
#else
                            throw std::runtime_error("Program compiled without USE_COMPRESSION. Cannot read compressed message");
#endif
                        }
                    }
                }
            }
        }
        catch (const std::exception& e)
        {
            LOG_FATAL("Exception:\n" << e.what());
            throw;
        }
        catch (...)
        {
            LOG_FATAL("Unknown exception");
            throw;
        }
    }

    void NetworkManager::ProcessPacket(const std::vector<unsigned char>& packet)
    {
        if (packet.empty())
        {
            return;
        }

        std::vector<unsigned char>::const_iterator packet_iterator = packet.begin();
        size_t length = packet.size();

        const int packet_id = ReadData<VarInt>(packet_iterator, length);

        std::shared_ptr<Message> msg = CreateClientboundMessage(state, packet_id);

        if (msg)
        {
            try
            {
                msg->Read(packet_iterator, length);
            }
            catch (const std::exception&)
            {
                LOG_FATAL("Parsing exception while parsing message \"" << msg->GetName() << '"');
                throw;
            }
            for (size_t i = 0; i < subscribed.size(); i++)
            {
                msg->Dispatch(subscribed[i]);
            }
        }
    }

    void NetworkManager::OnNewRawData(const std::vector<unsigned char>& packet)
    {
        {
            std::unique_lock<std::mutex> lck(mutex_process);
            packets_to_process.push(packet);
        }
        process_condition.notify_all();
    }


    void NetworkManager::Handle(ClientboundLoginCompressionPacket& msg)
    {
        compression = msg.GetCompressionThreshold();
    }

#if PROTOCOL_VERSION < 768 /* < 1.21.2 */
    void NetworkManager::Handle(ClientboundGameProfilePacket& msg)
#else
    void NetworkManager::Handle(ClientboundLoginFinishedPacket& msg)
#endif
    {
#if PROTOCOL_VERSION < 764 /* < 1.20.2 */
        state = ConnectionState::Play;
#else
        state = ConnectionState::Configuration;
        std::shared_ptr<ServerboundLoginAcknowledgedPacket> login_ack_msg = std::make_shared<ServerboundLoginAcknowledgedPacket>();
        Send(login_ack_msg);
#endif
    }

    void NetworkManager::Handle(ClientboundHelloPacket& msg)
    {
        if (authentifier == nullptr)
        {
            throw std::runtime_error("Authentication asked while no valid account has been provided, make sure to connect with a valid Microsoft Account, or to a server with online-mode=false");
        }

#ifdef USE_ENCRYPTION
        std::shared_ptr<AESEncrypter> encrypter = std::make_shared<AESEncrypter>();

        std::vector<unsigned char> raw_shared_secret;
        std::vector<unsigned char> encrypted_shared_secret;

#if PROTOCOL_VERSION < 759 /* < 1.19 */
        std::vector<unsigned char> encrypted_nonce;
        encrypter->Init(msg.GetPublicKey(), msg.GetNonce(),
            raw_shared_secret, encrypted_nonce, encrypted_shared_secret);
#elif PROTOCOL_VERSION < 761 /* < 1.19.3 */
        std::vector<unsigned char> salted_nonce_signature;
        long long int salt;
        encrypter->Init(msg.GetPublicKey(), msg.GetNonce(), authentifier->GetPrivateKey(),
            raw_shared_secret, encrypted_shared_secret,
            salt, salted_nonce_signature);
#else
        std::vector<unsigned char> encrypted_challenge;
        encrypter->Init(msg.GetPublicKey(), msg.GetChallenge(),
            raw_shared_secret, encrypted_shared_secret,
            encrypted_challenge);
#endif

        authentifier->JoinServer(msg.GetServerId(), raw_shared_secret, msg.GetPublicKey());

        std::shared_ptr<ServerboundKeyPacket> response_msg = std::make_shared<ServerboundKeyPacket>();
        response_msg->SetKeyBytes(encrypted_shared_secret);

#if PROTOCOL_VERSION < 759 /* < 1.19 */
        // Pre-1.19 behaviour, send encrypted nonce
        response_msg->SetNonce(encrypted_nonce);
#elif PROTOCOL_VERSION < 761 /* < 1.19.3 */
        // 1.19, 1.19.1, 1.19.2 behaviour, send salted nonce signature
        SaltSignature salt_signature;
        salt_signature.SetSalt(salt);
        salt_signature.SetSignature(salted_nonce_signature);
        response_msg->SetSaltSignature(salt_signature);
#else
        // 1.19.3+ behaviour, send encrypted challenge
        response_msg->SetEncryptedChallenge(encrypted_challenge);
#endif

        Send(response_msg);

        // Enable encryption from now on
        com->SetEncrypter(encrypter);
#else
        throw std::runtime_error("Your version of botcraft doesn't support encryption. Either run your server with online-mode=false or recompile botcraft");
#endif
    }

    void NetworkManager::Handle(ClientboundKeepAlivePacket& msg)
    {
        std::shared_ptr<ServerboundKeepAlivePacket> keep_alive_msg = std::make_shared<ServerboundKeepAlivePacket>();
        keep_alive_msg->SetId_(msg.GetId_());
        Send(keep_alive_msg);
    }

#if PROTOCOL_VERSION > 754 /* > 1.16.5 */
    void NetworkManager::Handle(ClientboundPingPacket& msg)
    {
        std::shared_ptr<ServerboundPongPacket> pong_msg = std::make_shared<ServerboundPongPacket>();
        pong_msg->SetId_(msg.GetId_());
        Send(pong_msg);
    }
#endif

#if PROTOCOL_VERSION > 340 /* > 1.12.2 */
    void NetworkManager::Handle(ClientboundCustomQueryPacket& msg)
    {
        // Vanilla like response when asked by fabric API
        // Not implemented in fabric before December 05 2020,
        // so not necessary before version 1.16.4
#if PROTOCOL_VERSION > 753 /* > 1.16.3 */
        if (msg.GetIdentifier().GetFull() == "fabric-networking-api-v1:early_registration")
        {
#if PROTOCOL_VERSION < 764 /* < 1.20.2 */
            std::shared_ptr<ServerboundCustomQueryPacket> custom_query_anwser = std::make_shared<ServerboundCustomQueryPacket>();
            custom_query_anwser->SetTransactionId(msg.GetTransactionId());
            custom_query_anwser->SetData(std::nullopt);
#else
            std::shared_ptr<ServerboundCustomQueryAnswerPacket> custom_query_anwser = std::make_shared<ServerboundCustomQueryAnswerPacket>();
            custom_query_anwser->SetTransactionId(msg.GetTransactionId());
            custom_query_anwser->SetPayload(std::nullopt);
#endif
            Send(custom_query_anwser);
            return;
        }
#endif
    }
#endif

#if PROTOCOL_VERSION > 759 /* > 1.19 */
    void NetworkManager::Handle(ClientboundPlayerChatPacket& msg)
    {
#if PROTOCOL_VERSION < 761 /* < 1.19.3 */
        chat_context.AddSeenMessage(msg.GetMessage_().GetHeaderSignature(), msg.GetMessage_().GetSignedHeader().GetSender());
#else
        if (msg.GetSignature().has_value())
        {
            std::scoped_lock<std::mutex> lock_messages(chat_context.GetMutex());

            chat_context.AddSeenMessage(std::vector<unsigned char>(msg.GetSignature().value().begin(), msg.GetSignature().value().end()));

            if (chat_context.GetOffset() > 64)
            {
                std::shared_ptr<ServerboundChatAckPacket> ack_msg = std::make_shared<ServerboundChatAckPacket>();
                ack_msg->SetOffset(chat_context.GetAndResetOffset());
                Send(ack_msg);
            }
        }
#endif
    }

#if PROTOCOL_VERSION < 761 /* < 1.19.3 */
    void NetworkManager::Handle(ClientboundPlayerChatHeaderPacket& msg)
    {
        chat_context.AddSeenMessage(msg.GetHeaderSignature(), msg.GetHeader().GetSender());
    }
#endif
#endif

#if PROTOCOL_VERSION > 760 /* > 1.19.2 */
    void NetworkManager::Handle(ClientboundLoginPacket& msg)
    {
        if (authentifier)
        {
            std::shared_ptr<ServerboundChatSessionUpdatePacket> chat_session_msg = std::make_shared<ServerboundChatSessionUpdatePacket>();
            RemoteChatSessionData chat_session_data;

            ProfilePublicKey key;
            key.SetTimestamp(authentifier->GetKeyTimestamp());
            key.SetKey(Utilities::RSAToBytes(authentifier->GetPublicKey()));
            key.SetSignature(Utilities::DecodeBase64(authentifier->GetKeySignature()));

            chat_session_data.SetProfilePublicKey(key);
            chat_session_uuid = UUID();
            std::mt19937 rnd = std::mt19937(static_cast<unsigned int>(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::high_resolution_clock::now().time_since_epoch()).count()));
            std::uniform_int_distribution<int> distrib(std::numeric_limits<unsigned char>::min(), std::numeric_limits<unsigned char>::max());
            for (size_t i = 0; i < chat_session_uuid.size(); ++i)
            {
                chat_session_uuid[i] = static_cast<unsigned char>(distrib(rnd));
            }
            chat_session_data.SetUuid(chat_session_uuid);

            chat_session_msg->SetChatSession(chat_session_data);
            Send(chat_session_msg);
        }
    }
#endif

#if PROTOCOL_VERSION > 763 /* > 1.20.1 */
    void NetworkManager::Handle(ClientboundFinishConfigurationPacket& msg)
    {
        state = ConnectionState::Play;
        std::shared_ptr<ServerboundFinishConfigurationPacket> finish_config_msg = std::make_shared<ServerboundFinishConfigurationPacket>();
        Send(finish_config_msg);
    }

    void NetworkManager::Handle(ClientboundKeepAliveConfigurationPacket& msg)
    {
        std::shared_ptr<ServerboundKeepAliveConfigurationPacket> keep_alive_msg = std::make_shared<ServerboundKeepAliveConfigurationPacket>();
        keep_alive_msg->SetId_(msg.GetId_());
        Send(keep_alive_msg);
    }

    void NetworkManager::Handle(ClientboundPingConfigurationPacket& msg)
    {
        std::shared_ptr<ServerboundPongConfigurationPacket> pong_msg = std::make_shared<ServerboundPongConfigurationPacket>();
        pong_msg->SetId_(msg.GetId_());
        Send(pong_msg);
    }

    void NetworkManager::Handle(ClientboundStartConfigurationPacket& msg)
    {
        state = ConnectionState::Configuration;
        std::shared_ptr<ServerboundConfigurationAcknowledgedPacket> config_ack_msg = std::make_shared<ServerboundConfigurationAcknowledgedPacket>();
        Send(config_ack_msg);
    }

    void NetworkManager::Handle(ClientboundChunkBatchStartPacket& msg)
    {
        chunk_batch_start_time = std::chrono::steady_clock::now();
    }

    void NetworkManager::Handle(ClientboundChunkBatchFinishedPacket& msg)
    {
        using count_return = decltype(std::declval<std::chrono::milliseconds>().count());
        const count_return time_elapsed_ms = std::max(static_cast<count_return>(1), std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - chunk_batch_start_time).count());
        std::shared_ptr<ServerboundChunkBatchReceivedPacket> chunk_per_tick_msg = std::make_shared<ServerboundChunkBatchReceivedPacket>();
        // Ask as many chunks as we can process in one tick (50 ms)
        chunk_per_tick_msg->SetDesiredChunksPerTick(msg.GetBatchSize() * 50.0f / time_elapsed_ms);
        Send(chunk_per_tick_msg);
    }
#endif

#if PROTOCOL_VERSION > 765 /* > 1.20.4 */
    void NetworkManager::Handle(ProtocolCraft::ClientboundSelectKnownPacksPacket& msg)
    {
        std::shared_ptr<ServerboundSelectKnownPacksPacket> select_known_packs = std::make_shared<ServerboundSelectKnownPacksPacket>();
        // Datapacks are not supported by Botcraft
        select_known_packs->SetKnownPacks({});

        Send(select_known_packs);

    }
#endif
}
