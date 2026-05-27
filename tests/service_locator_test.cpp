/// @file service_locator_test.cpp
/// @brief Tests for ServiceLocator dependency injection container.

#include <catch2/catch_test_macros.hpp>
#include <nk/foundation/service_locator.h>

class IDatabase {
public:
    virtual ~IDatabase() = default;
    virtual std::string query() const = 0;
};

class RealDatabase : public IDatabase {
public:
    std::string query() const override { return "real"; }
};

class MockDatabase : public IDatabase {
public:
    std::string query() const override { return "mock"; }
};

TEST_CASE("ServiceLocator registers and retrieves services", "[service_locator]") {
    nk::ServiceLocator::clear();

    REQUIRE_FALSE(nk::ServiceLocator::has<IDatabase>());
    
    nk::ServiceLocator::register_service<IDatabase>(std::make_shared<RealDatabase>());
    
    REQUIRE(nk::ServiceLocator::has<IDatabase>());
    auto db = nk::ServiceLocator::get<IDatabase>();
    REQUIRE(db->query() == "real");
}

TEST_CASE("ServiceLocator supports overriding services (for mocks)", "[service_locator]") {
    nk::ServiceLocator::clear();
    
    nk::ServiceLocator::register_service<IDatabase>(std::make_shared<RealDatabase>());
    nk::ServiceLocator::register_service<IDatabase>(std::make_shared<MockDatabase>());
    
    auto db = nk::ServiceLocator::get<IDatabase>();
    REQUIRE(db->query() == "mock");
}

TEST_CASE("ServiceLocator throws when requesting unregistered service", "[service_locator]") {
    nk::ServiceLocator::clear();
    
    REQUIRE_THROWS_AS(nk::ServiceLocator::get<IDatabase>(), std::runtime_error);
}

TEST_CASE("ServiceLocator generic registration helper works", "[service_locator]") {
    nk::ServiceLocator::clear();
    
    nk::ServiceLocator::register_service<RealDatabase>();
    auto db = nk::ServiceLocator::get<RealDatabase>();
    REQUIRE(db != nullptr);
    REQUIRE(db->query() == "real");
}