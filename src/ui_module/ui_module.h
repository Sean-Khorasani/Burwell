#ifndef BURWELL_UI_MODULE_H
#define BURWELL_UI_MODULE_H

#include <string>

namespace burwell {

class UIModule {
public:
    UIModule();
    std::string getUserInput();
    void displayFeedback(const std::string& message);
    void displayLog(const std::string& logEntry);
    bool promptUser(const std::string& question);
};

} // namespace burwell

#endif // BURWELL_UI_MODULE_H


