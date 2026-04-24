#include <exception>
#include <iostream>

int RunAllTests();

int main()
{
    try {
        return RunAllTests();
    } catch (const std::exception &ex) {
        std::cerr << "[fatal] " << ex.what() << std::endl;
        return 1;
    }
}
