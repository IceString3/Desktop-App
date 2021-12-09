#ifndef EXECUTABLE_SIGNATURE_H
#define EXECUTABLE_SIGNATURE_H

#include <QString>

// The class has the implementations:
// * ExecutableSignature_win
// * ExecutableSignature_mac
// * ExecutableSignature_linux
class ExecutableSignature
{
public:
    static bool isParentProcessGui();
    static bool verify(const QString &executablePath);
    static bool verifyWithSignCheck(const QString &executable);
};

#endif // EXECUTABLE_SIGNATURE_H
