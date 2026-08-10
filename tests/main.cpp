#include "catch2/catch_session.hpp"
#include "Logging.hpp"

int main(int argc, char* argv[]) {
    initLogger();
    int result = Catch::Session().run( argc, argv );
    return result;
}