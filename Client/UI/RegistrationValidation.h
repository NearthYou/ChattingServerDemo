#pragma once

#include <string>

namespace chat::ui
{
    const char* RegistrationValidationMessage(
        const std::string& nickname,
        const std::string& password);
}
