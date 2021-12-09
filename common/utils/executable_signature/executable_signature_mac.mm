#include "executable_signature_mac.h"
#include "executable_signature_defs.h"
#include <unistd.h>
#include <libproc.h>

#import <Security/Security.h>
#import <SystemConfiguration/SystemConfiguration.h>
#import <Foundation/Foundation.h>

#ifdef QT_CORE_LIB
#include <QCoreApplication>
#include <QDir>
#include "utils/logger.h"
#endif

bool ExecutableSignature_mac::verify(const std::string &exePath)
{
    // Check code signature.
    SecStaticCodeRef staticCode = NULL;
    NSString* path = [NSString stringWithCString:exePath.c_str()
                               encoding:[NSString defaultCStringEncoding]];

    OSStatus status = SecStaticCodeCreateWithPath((__bridge CFURLRef)([NSURL fileURLWithPath:path]), kSecCSDefaultFlags, &staticCode);

    if (status != errSecSuccess)
    {
        lastError_ = "SecStaticCodeCreateWithPath failed: " + std::to_string(status);
        return false;
    }

    // TODO: check signature (for some reason it doesn't work after extract app bundle with installer, so commented)
    /*
    SecCSFlags flags = kSecCSDefaultFlags;
    status = SecStaticCodeCheckValidity(staticCode, flags, NULL);
    if (status != errSecSuccess)
    {
        return false;
    }
    */

    CFDictionaryRef signingDetails = NULL;
    status = SecCodeCopySigningInformation(staticCode, kSecCSSigningInformation, &signingDetails);
    if (status != errSecSuccess)
    {
        lastError_ = "SecCodeCopySigningInformation failed: " + std::to_string(status);
        return false;
    }

    NSArray *certificateChain = [((__bridge NSDictionary*)signingDetails)
                                 objectForKey: (__bridge NSString*)kSecCodeInfoCertificates];
    if (certificateChain.count == 0)
    {
        lastError_ = "certificate chain is empty";
        return false;
    }

    bool certNameMatches = false;

    CFStringRef commonName = NULL;
    for (NSUInteger index = 0; index < certificateChain.count; index++)
    {
        SecCertificateRef certificate = (__bridge SecCertificateRef)([certificateChain objectAtIndex:index]);
        if ((errSecSuccess == SecCertificateCopyCommonName(certificate, &commonName)) && (NULL != commonName) )
        {
            if (CFEqual((CFTypeRef)commonName, (CFTypeRef)@MACOS_CERT_DEVELOPER_ID))
            {
                certNameMatches = true;
                break;
            }
         }
    }

    if (!certNameMatches) {
        lastError_ = std::string("No certificate common name matches for ") + std::string(MACOS_CERT_DEVELOPER_ID);
    }

    return certNameMatches;
}

#ifdef QT_CORE_LIB

bool ExecutableSignature_mac::isParentProcessGui()
{
    pid_t pid = getppid();
    char pathBuffer[PROC_PIDPATHINFO_MAXSIZE] = {0};
    int status = proc_pidpath(pid, pathBuffer, sizeof(pathBuffer));
    if ((status != 0) && (strlen(pathBuffer) != 0))
    {
        QString parentPath = QString::fromStdString(pathBuffer);
        QString guiPath = QCoreApplication::applicationDirPath() + "/../../../../MacOS/Windscribe";
        guiPath = QDir::cleanPath(guiPath);

        return (parentPath.compare(guiPath, Qt::CaseInsensitive) == 0) && verify(parentPath);
    }
    return false;
}

bool ExecutableSignature_mac::verify(const QString &executablePath)
{
    ExecutableSignature_mac verifySignature;
    bool result = verifySignature.verify(executablePath.toStdString());
    if (!result) {
        qCDebug(LOG_BASIC) << verifySignature.lastError().c_str();
    }
    return result;

}

// TODO: convert all uses of this to verify(...) once signature checking has been fixed for gui/engine check
bool ExecutableSignature_mac::verifyWithSignCheck(const QString &executablePath)
{
    //create static code ref via path
    SecStaticCodeRef staticCode = NULL;
    NSString* path = executablePath.toNSString();
    OSStatus status = SecStaticCodeCreateWithPath((__bridge CFURLRef)([NSURL fileURLWithPath:path]), kSecCSDefaultFlags, &staticCode);
    if (status != errSecSuccess)
    {
        return false;
    }

    SecCSFlags flags = kSecCSDefaultFlags;
    status = SecStaticCodeCheckValidity(staticCode, flags, NULL);
    if (status != errSecSuccess)
    {
        qDebug() << "Failed Signature Check";
        return false;
    }

    CFDictionaryRef signingDetails = NULL;
    status = SecCodeCopySigningInformation(staticCode, kSecCSSigningInformation, &signingDetails);
    if (status != errSecSuccess)
    {
        return false;
    }

    NSArray *certificateChain = [((__bridge NSDictionary*)signingDetails) objectForKey: (__bridge NSString*)kSecCodeInfoCertificates];
    if (certificateChain.count == 0)
    {
        // no certs
        return false;
    }

    CFStringRef commonName = NULL;
    for (NSUInteger index = 0; index < certificateChain.count; index++)
    {
        SecCertificateRef certificate = (__bridge SecCertificateRef)([certificateChain objectAtIndex:index]);
        if ((errSecSuccess == SecCertificateCopyCommonName(certificate, &commonName)) && (NULL != commonName) )
        {
            if (CFEqual((CFTypeRef)commonName, (CFTypeRef)@MACOS_CERT_DEVELOPER_ID))
            {
                return true;
            }
         }
    }

    return false;
}

#endif
