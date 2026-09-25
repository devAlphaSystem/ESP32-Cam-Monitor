#pragma once

#include <ctype.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace CameraWatchdog {
namespace CameraPolicy {

inline char lowerAscii(char value)
{
    return static_cast<char>(tolower(static_cast<unsigned char>(value)));
}

inline bool equalIgnoreCase(const char* left, const char* right)
{
    if (left == nullptr || right == nullptr)
        return false;
    while (*left != '\0' && *right != '\0') {
        if (lowerAscii(*left++) != lowerAscii(*right++))
            return false;
    }
    return *left == '\0' && *right == '\0';
}

inline bool normalizeIdentifier(const char* input, char output[129])
{
    output[0] = '\0';
    if (input == nullptr)
        return false;
    while (*input != '\0' && isspace(static_cast<unsigned char>(*input)))
        input++;
    size_t length = strlen(input);
    while (length != 0 && isspace(static_cast<unsigned char>(input[length - 1])))
        length--;
    const auto hasPrefix = [&](const char* prefix, size_t prefixLength) {
        if (length < prefixLength)
            return false;
        for (size_t index = 0; index < prefixLength; index++)
            if (lowerAscii(input[index]) != prefix[index])
                return false;
        return true;
    };
    if (hasPrefix("urn:uuid:", 9)) {
        input += 9;
        length -= 9;
    } else if (hasPrefix("uuid:", 5)) {
        input += 5;
        length -= 5;
    }
    if (length < 8 || length > 128)
        return false;
    bool hasNonzero = false;
    bool hasLetterOrDigit = false;
    for (size_t index = 0; index < length; index++) {
        const char value = lowerAscii(input[index]);
        if (!isalnum(static_cast<unsigned char>(value)) && value != '-' && value != '_' && value != '.' && value != ':')
            return false;
        if (value != '0' && value != '-' && value != ':' && value != '.')
            hasNonzero = true;
        if (isalnum(static_cast<unsigned char>(value)))
            hasLetterOrDigit = true;
        output[index] = value;
    }
    output[length] = '\0';
    if (!hasNonzero || !hasLetterOrDigit || equalIgnoreCase(output, "unknown") || equalIgnoreCase(output, "undefined") || equalIgnoreCase(output, "12345678") || equalIgnoreCase(output, "ffffffff") || equalIgnoreCase(output, "defaultid")) {
        output[0] = '\0';
        return false;
    }
    return true;
}

struct Identity {
    const char* endpointReference;
    const char* serialNumber;
    const char* manufacturer;
    const char* model;
    const char* interfaceMacs;
};

inline bool macsIntersect(const char* left, const char* right)
{
    if (left == nullptr || right == nullptr)
        return false;
    const char* cursor = left;
    while (*cursor != '\0') {
        const char* end = strchr(cursor, ',');
        const size_t length = end == nullptr ? strlen(cursor) : static_cast<size_t>(end - cursor);
        if (length == 17) {
            const char* candidate = right;
            while (*candidate != '\0') {
                const char* candidateEnd = strchr(candidate, ',');
                const size_t candidateLength = candidateEnd == nullptr ? strlen(candidate) : static_cast<size_t>(candidateEnd - candidate);
                if (candidateLength == length && strncmp(candidate, cursor, length) == 0)
                    return true;
                if (candidateEnd == nullptr)
                    break;
                candidate = candidateEnd + 1;
            }
        }
        if (end == nullptr)
            break;
        cursor = end + 1;
    }
    return false;
}

inline bool identifiersConflict(const Identity& left, const Identity& right)
{
    char leftEpr[129], rightEpr[129], leftSerial[129], rightSerial[129];
    const bool hasLeftEpr = normalizeIdentifier(left.endpointReference, leftEpr);
    const bool hasRightEpr = normalizeIdentifier(right.endpointReference, rightEpr);
    const bool hasLeftSerial = normalizeIdentifier(left.serialNumber, leftSerial);
    const bool hasRightSerial = normalizeIdentifier(right.serialNumber, rightSerial);
    return (hasLeftEpr && hasRightEpr && strcmp(leftEpr, rightEpr) != 0) || (hasLeftSerial && hasRightSerial && strcmp(leftSerial, rightSerial) != 0);
}

inline bool sameIdentity(const Identity& left, const Identity& right)
{
    if (identifiersConflict(left, right))
        return false;
    char leftEpr[129], rightEpr[129], leftSerial[129], rightSerial[129];
    const bool eprMatch = normalizeIdentifier(left.endpointReference, leftEpr) && normalizeIdentifier(right.endpointReference, rightEpr) && strcmp(leftEpr, rightEpr) == 0;
    const bool serialMatch = normalizeIdentifier(left.serialNumber, leftSerial) && normalizeIdentifier(right.serialNumber, rightSerial) && strcmp(leftSerial, rightSerial) == 0 && left.manufacturer != nullptr && left.model != nullptr && left.manufacturer[0] != '\0' && left.model[0] != '\0' && equalIgnoreCase(left.manufacturer, right.manufacturer) && equalIgnoreCase(left.model, right.model);
    const bool sharedInterface = macsIntersect(left.interfaceMacs, right.interfaceMacs);
    if (eprMatch && serialMatch) {
        if (left.interfaceMacs != nullptr && right.interfaceMacs != nullptr && left.interfaceMacs[0] != '\0' && right.interfaceMacs[0] != '\0' && !sharedInterface)
            return false;
        return true;
    }
    return (eprMatch || serialMatch) && sharedInterface;
}

inline bool allowEndpointPromotion(unsigned failureStreak, bool alternateResponded, bool identityVerified)
{
    return failureStreak >= 2 && alternateResponded && identityVerified;
}

inline bool safeHttpPath(const char* path, size_t maximumLength, bool allowQuery)
{
    if (path == nullptr || path[0] != '/')
        return false;
    const size_t length = strlen(path);
    if (length == 0 || length > maximumLength)
        return false;
    for (size_t index = 0; index < length; index++) {
        const unsigned char value = static_cast<unsigned char>(path[index]);
        if (value < 33 || value == 127 || value == '\\' || value == '#')
            return false;
        if (!allowQuery && value == '?')
            return false;
        if (value == '.' && index + 1 < length && path[index + 1] == '.')
            return false;
    }
    return true;
}

inline bool hasXmlLocalOpeningTag(const char* xml, const char* localName)
{
    if (xml == nullptr || localName == nullptr)
        return false;
    const size_t expectedLength = strlen(localName);
    for (const char* cursor = xml; *cursor != '\0'; cursor++) {
        if (strncmp(cursor, "<!--", 4) == 0) {
            const char* end = strstr(cursor + 4, "-->");
            if (end == nullptr)
                return false;
            cursor = end + 2;
            continue;
        }
        if (strncmp(cursor, "<![", 3) == 0) {
            const char* end = strstr(cursor + 3, "]]>");
            if (end == nullptr)
                return false;
            cursor = end + 2;
            continue;
        }
        if (*cursor != '<' || cursor[1] == '/' || cursor[1] == '!' || cursor[1] == '?')
            continue;
        const char* nameStart = cursor + 1;
        const char* nameEnd = nameStart;
        while (*nameEnd != '\0' && (isalnum(static_cast<unsigned char>(*nameEnd)) || *nameEnd == '_' || *nameEnd == ':' || *nameEnd == '-'))
            nameEnd++;
        if (nameEnd == nameStart || (*nameEnd != '>' && *nameEnd != '/' && !isspace(static_cast<unsigned char>(*nameEnd))))
            continue;
        const char* localStart = nameStart;
        for (const char* part = nameStart; part < nameEnd; part++)
            if (*part == ':')
                localStart = part + 1;
        if (static_cast<size_t>(nameEnd - localStart) == expectedLength && strncmp(localStart, localName, expectedLength) == 0)
            return true;
        cursor = nameEnd - 1;
    }
    return false;
}

inline uint32_t checksumBytes(const void* values, size_t length)
{
    const uint8_t* bytes = static_cast<const uint8_t*>(values);
    uint32_t hash = 2166136261UL;
    for (size_t index = 0; index < length; index++) {
        hash ^= bytes[index];
        hash *= 16777619UL;
    }
    return hash;
}

inline bool committedAliasCanHide(uint32_t secondaryId, uint32_t primaryId, bool aliasChecksumValid, bool primaryChecksumValid)
{
    return secondaryId != 0 && primaryId != 0 && secondaryId != primaryId && aliasChecksumValid && primaryChecksumValid;
}

enum class StoredRecordLength {
    MissingReserved,
    Present,
    Invalid
};

inline StoredRecordLength classifyStoredRecordLength(size_t bytes, size_t minimum, size_t maximum)
{
    if (bytes == 0)
        return StoredRecordLength::MissingReserved;
    return bytes >= minimum && bytes <= maximum ? StoredRecordLength::Present : StoredRecordLength::Invalid;
}

inline uint8_t combinedRebootReservations(uint8_t primaryCount, uint8_t secondaryCount, uint8_t capacity)
{
    const unsigned combined = static_cast<unsigned>(primaryCount) + secondaryCount;
    return static_cast<uint8_t>(combined < capacity ? combined : capacity);
}

}
}
