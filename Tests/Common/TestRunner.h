#pragma once

#include <iostream>
#include <string_view>

class FTestRunner
{
public:
    void Expect(bool Condition, std::string_view Description)
    {
        if (Condition)
        {
            std::cout << "[PASS] " << Description << '\n';
            ++PassedCount;
            return;
        }

        std::cerr << "[FAIL] " << Description << '\n';
        ++FailedCount;
    }

    int Finish() const
    {
        std::cout << "\n" << PassedCount << " passed, " << FailedCount << " failed.\n";
        return FailedCount == 0 ? 0 : 1;
    }

private:
    int PassedCount = 0;
    int FailedCount = 0;
};
