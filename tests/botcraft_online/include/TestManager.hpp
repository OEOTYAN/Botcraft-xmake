#pragma once

#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>

#include <filesystem>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include <botcraft/AI/BehaviourClient.hpp>
#include <botcraft/Game/Enums.hpp>
#include <botcraft/Game/ManagersClient.hpp>
#include <botcraft/Game/Vector3.hpp>
#include <botcraft/Game/World/World.hpp>
#include <botcraft/Utilities/SleepUtilities.hpp>

#include "MinecraftServer.hpp"


/// @brief Singleton class that organizes the layout of the tests
/// in the world and sets them up.
class TestManager
{
private:
    TestManager();
    ~TestManager();
    TestManager(const TestManager&) = delete;
    TestManager(TestManager&&) = delete;
    TestManager& operator=(const TestManager&) = delete;

    /// @brief Spacing between tests
    static constexpr int spacing_x = 3;
    /// @brief Spacing between sections
    static constexpr int spacing_z = 3;


public:
    static TestManager& GetInstance();

    /// @brief current_offset getter
    /// @return The offset in the world of the currently running section
    const Botcraft::Position& GetCurrentOffset() const;

#if PROTOCOL_VERSION > 340 /* > 1.12.2 */
    void SetBlock(const std::string& name, const Botcraft::Position& pos, const std::map<std::string, std::string>& blockstates = {}, const std::map<std::string, std::string>& metadata = {}, const bool escape_metadata = true) const;
#else
    void SetBlock(const std::string& name, const Botcraft::Position& pos, const int block_variant = 0, const std::map<std::string, std::string>& metadata = {}, const bool escape_metadata = true) const;
#endif

    /// @brief Create a book with given content at pos
    /// @param pos Position of the item frame/lectern
    /// @param pages Content of the pages of the book
    /// @param facing Orientation of the item frame/lectern (book is toward this direction)
    /// @param title Title of the book
    /// @param author Author of the book
    /// @param description Description of the book (minecraft tooltip)
    void CreateBook(const Botcraft::Position& pos, const std::vector<std::string>& pages, const std::string& facing = "north", const std::string& title = "", const std::string& author = "", const std::vector<std::string>& description = {});

    void Teleport(const std::string& name, const Botcraft::Vector3<double>& pos, const float yaw = 0.0f, const float pitch = 0.0f) const;

    const std::filesystem::path& GetPhysicsRecapPath() const;

    template<class ClientType = Botcraft::ManagersClient,
        std::enable_if_t<std::is_base_of_v<Botcraft::ConnectionClient, ClientType>, bool> = true>
    std::unique_ptr<ClientType> GetBot(std::string& botname, int& id, Botcraft::Vector3<double>& pos, const Botcraft::GameType gamemode = Botcraft::GameType::Survival)
    {
        std::unique_ptr<ClientType> client;
        if constexpr (std::is_same_v<ClientType, Botcraft::ConnectionClient>)
        {
            client = std::make_unique<ClientType>();
        }
        else
        {
            client = std::make_unique<ClientType>(false);
        }

        botname = "botcraft_" + std::to_string(bot_index++);
        client->Connect("127.0.0.1:25565", botname);

        std::vector<std::string> captured = MinecraftServer::GetInstance().WaitLine(".*?" + botname + ".*? logged in with entity id (\\d+) at \\(([\\d.,]+), ([\\d.,]+), ([\\d.,]+)\\).*", 5000);
        id = std::stoi(captured[1]);
        pos = Botcraft::Vector3(std::stod(captured[2]), std::stod(captured[3]), std::stod(captured[4]));

        MinecraftServer::GetInstance().WaitLine(".*?" + botname + " joined the game.*", 5000);

        if (gamemode != Botcraft::GameType::Creative)
        {
            SetGameMode(botname, gamemode);
        }

        if constexpr (std::is_same_v<ClientType, Botcraft::ConnectionClient>)
        {
            return client;
        }
        else
        {
            // If this is not a ConnectionClient, wait for a good amount of chunks to be loaded
            const int num_chunk_to_load = (MinecraftServer::options.view_distance + 1) * (MinecraftServer::options.view_distance + 1);
            if (!Botcraft::Utilities::WaitForCondition([&]()
                {
                    std::shared_ptr<Botcraft::World> world = client->GetWorld();
                    // If the client has not received a ClientboundGameProfilePacket yet world is still nullptr
                    if (world != nullptr && world->GetChunks()->size() >= num_chunk_to_load)
                    {
                        return true;
                    }
                    return false;
                }, 10000))
            {
                throw std::runtime_error(botname + " took too long to load world");
            }

            if constexpr (std::is_base_of_v<Botcraft::BehaviourClient, ClientType>)
            {
                client->StartBehaviour();
            }

            return client;
        }
    }

    template<class ClientType = Botcraft::ManagersClient,
        std::enable_if_t<std::is_base_of_v<Botcraft::ConnectionClient, ClientType>, bool> = true>
    std::unique_ptr<ClientType> GetBot(std::string& botname, const Botcraft::GameType gamemode = Botcraft::GameType::Survival)
    {
        int id;
        Botcraft::Vector3<double> pos;
        return GetBot<ClientType>(botname, id, pos, gamemode);
    }


    template<class ClientType = Botcraft::ManagersClient,
        std::enable_if_t<std::is_base_of_v<Botcraft::ConnectionClient, ClientType>, bool> = true>
    std::unique_ptr<ClientType> GetBot(std::string& botname, Botcraft::Vector3<double>& pos, const Botcraft::GameType gamemode = Botcraft::GameType::Survival)
    {
        int id;
        return GetBot<ClientType>(botname, id, pos, gamemode);
    }

    template<class ClientType = Botcraft::ManagersClient,
        std::enable_if_t<std::is_base_of_v<Botcraft::ConnectionClient, ClientType>, bool> = true>
    std::unique_ptr<ClientType> GetBot(const Botcraft::GameType gamemode = Botcraft::GameType::Survival)
    {
        std::string botname;
        int id;
        Botcraft::Vector3<double> pos;
        return GetBot<ClientType>(botname, id, pos, gamemode);
    }

private:
    enum class TestSucess
    {
        None,
        Success,
        Failure,
        ExpectedFailure,
        Skipped,
    };

    /// @brief Read a NBT structure file and extract its size
    /// @return A X/Y/Z size Vector3
    Botcraft::Position GetStructureSize(const std::string& filename) const;

    /// @brief Create a TP sign
    /// @param src Position of the sign.
    /// @param dst TP destination coordinates
    /// @param texts A list of strings to display on the sign
    /// @param facing The direction the sign will face
    /// @param success A TestSuccess result, will define wood and/or text color depending on minecraft version
    void CreateTPSign(const Botcraft::Position& src, const Botcraft::Vector3<double>& dst, const std::vector<std::string>& texts, const std::string& facing = "north", const TestSucess success = TestSucess::None) const;

    /// @brief Load a structure block into the world
    /// @param filename The structure block to load. If it doesn't exist, will use "_default" instead
    /// @param pos position of the structure block
    /// @param load_offset offset to load the structure to (w.r.t pos), default to (0,0,0)
    void LoadStructure(const std::string& filename, const Botcraft::Position& pos, const Botcraft::Position& load_offset = Botcraft::Position()) const;

    /// @brief Make sure a block is loaded on the server by teleporting the chunk_loader
    /// @param pos The position to load
    void MakeSureLoaded(const Botcraft::Position& pos) const;

    /// @brief Set a bot gamemode
    /// @param name Bot name
    /// @param gamemode Target game mode
    void SetGameMode(const std::string& name, const Botcraft::GameType gamemode) const;


    friend class TestManagerListener;
    void testRunStarting(Catch::TestRunInfo const& test_run_info);
    void testCaseStarting(Catch::TestCaseInfo const& test_info);
    void testCasePartialStarting(Catch::TestCaseInfo const& test_info, uint64_t part_number);
    void sectionStarting(Catch::SectionInfo const& section_info);
    void assertionEnded(Catch::AssertionStats const& assertion_stats);
    void testCasePartialEnded(Catch::TestCaseStats const& test_case_stats, uint64_t part_number);
    void testCaseEnded(Catch::TestCaseStats const& test_case_stats);
    void testRunEnded(Catch::TestRunStats const& test_run_info);

private:
    /// @brief Offset in the world for current section/test
    Botcraft::Position current_offset;
    /// @brief Size of the header structure
    Botcraft::Position header_size;

    /// @brief Index of the next bot to be created
    int bot_index;
    /// @brief Bot used to load chunks before running commands on them
    std::unique_ptr<Botcraft::ManagersClient> chunk_loader;
    /// @brief Name of the bot used as chunk loader
    std::string chunk_loader_name;

    /// @brief Index of the current test in the world
    int current_test_index;
    /// @brief Size of the loaded structure for current test
    Botcraft::Position current_test_size;
    /// @brief All failed assertions in this test case so far
    std::vector<std::string> current_test_case_failures;

    /// @brief Store names of all running (potentially) nested sections
    std::vector<std::string> section_stack;
    /// @brief Position of the header for current section
    Botcraft::Position current_header_position;

    /// @brief Path to the physics recap markdown file
    std::filesystem::path physics_recap_path;
};

/// @brief Catch2 listener to get test events and pass them to the main singleton
/// that can be easily accessed from inside the tests. Maybe there is an easier dedicated
/// way to do it without a singleton but couldn't find it.
class TestManagerListener : public Catch::EventListenerBase
{
    using Catch::EventListenerBase::EventListenerBase;

    void testRunStarting(Catch::TestRunInfo const& test_run_info) override;
    void testCaseStarting(Catch::TestCaseInfo const& test_info) override;
    void testCasePartialStarting(Catch::TestCaseInfo const& test_info, uint64_t part_number) override;
    void sectionStarting(Catch::SectionInfo const& section_info) override;
    void assertionEnded(Catch::AssertionStats const& assertion_stats) override;
    void testCasePartialEnded(Catch::TestCaseStats const& test_case_stats, uint64_t part_number) override;
    void testCaseEnded(Catch::TestCaseStats const& test_case_stats) override;
    void testRunEnded(Catch::TestRunStats const& test_run_info) override;
};
