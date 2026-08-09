#pragma once

struct LiveProfile {
    bool valid = false;
    String token;
    String name;
    String encoding;
    uint16_t width = 0;
    uint16_t height = 0;
    uint16_t frameRate = 0;
    uint32_t bitrateKbps = 0;
    bool preferred = false;
    String streamUri;
};

LiveStreamRelay liveStreamRelay;
SemaphoreHandle_t liveCatalogMutex = nullptr;
LiveProfile liveProfiles[MAX_LIVE_PROFILES];
size_t liveProfileCount = 0;
String liveMediaUrl;
IPAddress liveCameraIp;
uint16_t liveCameraRtspPort = 554;
bool liveCatalogReady = false;
String liveCatalogResult = "Perfis ao vivo ainda nao consultados";
unsigned long liveCatalogUpdatedAtMs = 0;
bool liveRelayStarted = false;
String liveRelayStartResult = "Relay ainda nao inicializado";
char liveRelayUserAgent[64] = {0};

bool ensureLiveCatalogMutex()
{
    if (liveCatalogMutex != nullptr) {
        return true;
    }

    liveCatalogMutex = xSemaphoreCreateMutex();
    return liveCatalogMutex != nullptr;
}

bool takeLiveCatalogMutex(TickType_t timeout = pdMS_TO_TICKS(250))
{
    return
        ensureLiveCatalogMutex() &&
        xSemaphoreTake(liveCatalogMutex, timeout) == pdTRUE;
}

void giveLiveCatalogMutex()
{
    if (liveCatalogMutex != nullptr) {
        xSemaphoreGive(liveCatalogMutex);
    }
}

String normalizedLiveEncoding(String encoding)
{
    encoding.trim();
    encoding.toUpperCase();
    encoding.replace(".", "");
    encoding.replace("-", "");

    if (encoding == "AVC" || encoding == "AVC1") {
        return "H264";
    }

    if (encoding == "HEVC") {
        return "H265";
    }

    return encoding;
}

bool liveEncodingCompatible(const String& encoding)
{
    const String normalized = normalizedLiveEncoding(encoding);
    return
        normalized.isEmpty() ||
        normalized == "UNKNOWN" ||
        normalized == "H264";
}

void populateLiveStreamDescriptor(
    const LiveProfile& profile,
    const String& streamUri,
    LiveStreamDescriptor& descriptor
)
{
    copySettingText(descriptor.uri, streamUri);
    copySettingText(descriptor.profileName, profile.name);

    if (profile.token.length() <= LIVE_STREAM_PROFILE_TOKEN_MAX_LENGTH) {
        copySettingText(descriptor.profileToken, profile.token);
    } else {
        descriptor.profileToken[0] = '\0';
    }

    copySettingText(descriptor.encoding, profile.encoding);
    descriptor.width = profile.width;
    descriptor.height = profile.height;
    descriptor.declaredFps = profile.frameRate;
    descriptor.declaredBitrateKbps = profile.bitrateKbps;
}

void invalidateLiveProfiles()
{
    lastRelayMediaEvidenceConsumedAtMs = 0;
    resetMediaCanaryFailureEvidence();
    liveStreamRelay.invalidateMediaEvidence();

    if (!takeLiveCatalogMutex()) {
        return;
    }

    for (size_t index = 0; index < MAX_LIVE_PROFILES; index++) {
        liveProfiles[index] = LiveProfile{};
    }

    liveProfileCount = 0;
    liveMediaUrl = String();
    liveCameraIp = IPAddress();
    liveCameraRtspPort = 554;
    liveCatalogReady = false;
    liveCatalogResult = "Perfis invalidados; aguardando nova consulta ONVIF";
    liveCatalogUpdatedAtMs = millis();
    giveLiveCatalogMutex();
}

String liveProfileSection(
    const String& profileXml,
    const char* openingTag,
    const char* nextOpeningTag
)
{
    int sectionStart = 0;
    int sectionTagEnd = 0;

    if (!findNextOpeningTag(
        profileXml,
        openingTag,
        0,
        sectionStart,
        sectionTagEnd
    )) {
        return String();
    }

    int sectionEnd = profileXml.length();
    int ignoredEnd = 0;

    if (nextOpeningTag != nullptr) {
        findNextOpeningTag(
            profileXml,
            nextOpeningTag,
            sectionTagEnd + 1,
            sectionEnd,
            ignoredEnd
        );
    }

    return profileXml.substring(sectionStart, sectionEnd);
}

uint32_t liveUnsignedTag(const String& xml, const char* localName)
{
    String value;
    int ignored = 0;

    if (!extractLocalTag(xml, localName, 0, value, ignored)) {
        return 0;
    }

    value.trim();

    for (size_t index = 0; index < value.length(); index++) {
        if (!isdigit(static_cast<unsigned char>(value.charAt(index)))) {
            return 0;
        }
    }

    return static_cast<uint32_t>(value.toInt());
}

size_t collectLiveProfiles(
    const String& profilesXml,
    LiveProfile (&profiles)[MAX_LIVE_PROFILES]
)
{
    size_t count = 0;
    int searchPosition = 0;

    while (count < MAX_LIVE_PROFILES) {
        int profileStart = 0;
        int profileTagEnd = 0;

        if (!findNextOpeningTag(
            profilesXml,
            "Profiles",
            searchPosition,
            profileStart,
            profileTagEnd
        )) {
            break;
        }

        int nextProfileStart = profilesXml.length();
        int ignoredTagEnd = 0;
        findNextOpeningTag(
            profilesXml,
            "Profiles",
            profileTagEnd + 1,
            nextProfileStart,
            ignoredTagEnd
        );

        const String openingTag =
            profilesXml.substring(profileStart, profileTagEnd + 1);
        String token;

        if (!extractXmlAttribute(openingTag, "token", token)) {
            searchPosition = profileTagEnd + 1;
            continue;
        }

        const String profileXml =
            profilesXml.substring(profileTagEnd + 1, nextProfileStart);
        LiveProfile profile;
        profile.valid = true;
        profile.token = xmlUnescape(token);
        profile.token.trim();

        int ignoredPosition = 0;

        if (!extractLocalTag(
            profileXml,
            "Name",
            0,
            profile.name,
            ignoredPosition
        )) {
            profile.name = profile.token;
        } else {
            profile.name = xmlUnescape(profile.name);
            profile.name.trim();

            if (profile.name.isEmpty()) {
                profile.name = profile.token;
            }
        }

        const String encoderXml = liveProfileSection(
            profileXml,
            "VideoEncoderConfiguration",
            "AudioEncoderConfiguration"
        );

        if (!encoderXml.isEmpty()) {
            String encoding;
            ignoredPosition = 0;
            extractLocalTag(
                encoderXml,
                "Encoding",
                0,
                encoding,
                ignoredPosition
            );
            profile.encoding = normalizedLiveEncoding(encoding);
            profile.width = static_cast<uint16_t>(min(
                liveUnsignedTag(encoderXml, "Width"),
                static_cast<uint32_t>(65535)
            ));
            profile.height = static_cast<uint16_t>(min(
                liveUnsignedTag(encoderXml, "Height"),
                static_cast<uint32_t>(65535)
            ));
            profile.frameRate = static_cast<uint16_t>(min(
                liveUnsignedTag(encoderXml, "FrameRateLimit"),
                static_cast<uint32_t>(65535)
            ));
            profile.bitrateKbps = liveUnsignedTag(
                encoderXml,
                "BitrateLimit"
            );
        }

        if (profile.encoding.isEmpty()) {
            profile.encoding = "UNKNOWN";
        }

        profiles[count++] = profile;
        searchPosition = profileTagEnd + 1;
    }

    int preferredIndex = -1;
    uint32_t preferredPixels = UINT32_MAX;
    uint32_t preferredBitrate = UINT32_MAX;

    for (size_t index = 0; index < count; index++) {
        if (normalizedLiveEncoding(profiles[index].encoding) != "H264") {
            continue;
        }

        const uint32_t pixels =
            profiles[index].width > 0 && profiles[index].height > 0
                ? static_cast<uint32_t>(profiles[index].width) *
                    profiles[index].height
                : UINT32_MAX;
        const uint32_t bitrate = profiles[index].bitrateKbps > 0
            ? profiles[index].bitrateKbps
            : UINT32_MAX;

        if (
            preferredIndex < 0 ||
            pixels < preferredPixels ||
            (pixels == preferredPixels && bitrate < preferredBitrate)
        ) {
            preferredIndex = static_cast<int>(index);
            preferredPixels = pixels;
            preferredBitrate = bitrate;
        }
    }

    if (preferredIndex < 0 && count > 0) {
        preferredIndex = 0;
    }

    if (preferredIndex >= 0) {
        profiles[preferredIndex].preferred = true;
    }

    return count;
}

void publishLiveProfilesFromXml(
    const String& profilesXml,
    const String& mediaUrl,
    const IPAddress& cameraIp
)
{
    LiveProfile parsed[MAX_LIVE_PROFILES];
    const size_t count = collectLiveProfiles(profilesXml, parsed);

    if (!takeLiveCatalogMutex()) {
        return;
    }

    bool profileIdentityChanged =
        liveMediaUrl != mediaUrl ||
        liveCameraIp != cameraIp ||
        liveProfileCount != count;

    if (!profileIdentityChanged) {
        for (size_t index = 0; index < count; index++) {
            if (
                liveProfiles[index].valid != parsed[index].valid ||
                liveProfiles[index].token != parsed[index].token ||
                normalizedLiveEncoding(liveProfiles[index].encoding) !=
                    normalizedLiveEncoding(parsed[index].encoding)
            ) {
                profileIdentityChanged = true;
                break;
            }
        }
    }

    if (profileIdentityChanged) {
        resetMediaCanaryFailureEvidence();
    }

    if (liveMediaUrl == mediaUrl && liveCameraIp == cameraIp) {
        for (size_t parsedIndex = 0; parsedIndex < count; parsedIndex++) {
            for (size_t currentIndex = 0; currentIndex < liveProfileCount; currentIndex++) {
                if (
                    liveProfiles[currentIndex].valid &&
                    liveProfiles[currentIndex].token == parsed[parsedIndex].token
                ) {
                    parsed[parsedIndex].streamUri =
                        liveProfiles[currentIndex].streamUri;
                    break;
                }
            }
        }
    }

    for (size_t index = 0; index < MAX_LIVE_PROFILES; index++) {
        liveProfiles[index] = index < count ? parsed[index] : LiveProfile{};
    }

    liveProfileCount = count;
    liveMediaUrl = mediaUrl;
    liveCameraIp = cameraIp;
    liveCameraRtspPort = camera.rtspPort;
    liveCatalogReady = count > 0 && !mediaUrl.isEmpty();
    liveCatalogUpdatedAtMs = millis();
    liveCatalogResult = liveCatalogReady
        ? String("Perfis ONVIF carregados: ") + String(count)
        : "ONVIF nao retornou perfis de video utilizaveis";
    giveLiveCatalogMutex();
}

bool copyLiveProfile(
    size_t profileIndex,
    LiveProfile& profile,
    String& mediaUrl,
    IPAddress& cameraIp,
    uint16_t& cameraRtspPort,
    String& error
)
{
    if (!takeLiveCatalogMutex()) {
        error = "Catalogo ao vivo ocupado; tente novamente";
        return false;
    }

    const bool valid =
        liveCatalogReady &&
        profileIndex < liveProfileCount &&
        liveProfiles[profileIndex].valid;

    if (valid) {
        profile = liveProfiles[profileIndex];
        mediaUrl = liveMediaUrl;
        cameraIp = liveCameraIp;
        cameraRtspPort = liveCameraRtspPort;
    }

    giveLiveCatalogMutex();

    if (!valid) {
        error = "Perfil ONVIF inexistente ou catalogo indisponivel";
    }

    return valid;
}

size_t collectCompatibleLiveProfileIndices(
    size_t* profileIndices,
    size_t capacity
)
{
    if (
        profileIndices == nullptr ||
        capacity == 0 ||
        !takeLiveCatalogMutex()
    ) {
        return 0;
    }

    size_t count = 0;
    int preferredIndex = -1;

    for (size_t index = 0; index < liveProfileCount; index++) {
        const LiveProfile& profile = liveProfiles[index];

        if (
            profile.valid &&
            profile.preferred &&
            liveEncodingCompatible(profile.encoding)
        ) {
            preferredIndex = static_cast<int>(index);
            break;
        }
    }

    if (preferredIndex >= 0 && count < capacity) {
        profileIndices[count++] = static_cast<size_t>(preferredIndex);
    }

    for (
        size_t index = 0;
        index < liveProfileCount && count < capacity;
        index++
    ) {
        const LiveProfile& profile = liveProfiles[index];

        if (
            static_cast<int>(index) != preferredIndex &&
            profile.valid &&
            liveEncodingCompatible(profile.encoding)
        ) {
            profileIndices[count++] = index;
        }
    }

    giveLiveCatalogMutex();
    return count;
}

bool findPreferredLiveProfileIndex(size_t& profileIndex)
{
    size_t indices[MAX_LIVE_PROFILES];
    const size_t count = collectCompatibleLiveProfileIndices(
        indices,
        MAX_LIVE_PROFILES
    );

    if (count == 0) {
        return false;
    }

    profileIndex = indices[0];
    return true;
}

String normalizeLiveRtspUri(
    const String& returnedUri,
    const IPAddress& cameraIp,
    uint16_t fallbackPort
)
{
    String uri = returnedUri;
    uri.trim();
    String lower = uri;
    lower.toLowerCase();

    if (!lower.startsWith("rtsp://")) {
        return String();
    }

    const int authorityStart = 7;
    const int slashStart = uri.indexOf('/', authorityStart);
    const int queryStart = uri.indexOf('?', authorityStart);
    int suffixStart = -1;

    if (slashStart >= 0 && queryStart >= 0) {
        suffixStart = min(slashStart, queryStart);
    } else if (slashStart >= 0) {
        suffixStart = slashStart;
    } else if (queryStart >= 0) {
        suffixStart = queryStart;
    }

    const int authorityEnd = suffixStart >= 0 ? suffixStart : uri.length();
    String authority = uri.substring(authorityStart, authorityEnd);
    const int atPosition = authority.lastIndexOf('@');

    if (atPosition >= 0) {
        authority = authority.substring(atPosition + 1);
    }

    uint16_t port = fallbackPort > 0 ? fallbackPort : 554;
    const int portSeparator = authority.lastIndexOf(':');

    if (portSeparator > 0) {
        const String portText = authority.substring(portSeparator + 1);
        bool numeric = !portText.isEmpty();

        for (size_t index = 0; index < portText.length(); index++) {
            numeric = numeric && isdigit(
                static_cast<unsigned char>(portText.charAt(index))
            );
        }

        if (numeric) {
            const long parsedPort = portText.toInt();

            if (parsedPort > 0 && parsedPort <= 65535) {
                port = static_cast<uint16_t>(parsedPort);
            }
        }
    }

    String normalized = "rtsp://";
    normalized += cameraIp.toString();

    if (port != 554) {
        normalized += ':';
        normalized += port;
    }

    const String path = suffixStart >= 0 ? uri.substring(suffixStart) : "/";

    if (!path.startsWith("/")) {
        normalized += '/';
    }

    normalized += path;
    return normalized;
}

bool provideLiveStreamDescriptor(
    size_t profileIndex,
    LiveStreamDescriptor& descriptor,
    String& error,
    uint32_t timeoutMs
)
{
    const uint32_t descriptorStartedAtMs = millis();
    descriptor = LiveStreamDescriptor{};
    LiveProfile profile;
    String mediaUrl;
    IPAddress cameraIp;
    uint16_t cameraRtspPort = 554;

    if (!copyLiveProfile(
        profileIndex,
        profile,
        mediaUrl,
        cameraIp,
        cameraRtspPort,
        error
    )) {
        return false;
    }

    if (!liveEncodingCompatible(profile.encoding)) {
        error = String("O perfil ") + profile.name +
            " usa " + profile.encoding + "; o relay integrado exige H.264";
        return false;
    }

    String streamUri = profile.streamUri;

    if (!streamUri.isEmpty()) {
        populateLiveStreamDescriptor(profile, streamUri, descriptor);
        return true;
    }

    String requestBody;
    requestBody.reserve(profile.token.length() + 420);
    requestBody += "<trt:GetStreamUri><trt:StreamSetup>";
    requestBody += "<tt:Stream xmlns:tt=\"http://www.onvif.org/ver10/schema\">";
    requestBody += "RTP-Unicast</tt:Stream><tt:Transport ";
    requestBody += "xmlns:tt=\"http://www.onvif.org/ver10/schema\">";
    requestBody += "<tt:Protocol>RTSP</tt:Protocol></tt:Transport>";
    requestBody += "</trt:StreamSetup><trt:ProfileToken>";
    requestBody += htmlEscape(profile.token);
    requestBody += "</trt:ProfileToken></trt:GetStreamUri>";

    String streamXml;
    int statusCode = 0;
    const uint32_t descriptorElapsedMs = millis() - descriptorStartedAtMs;

    if (descriptorElapsedMs >= timeoutMs) {
        error = "Timeout antes de consultar GetStreamUri";
        return false;
    }

    const uint32_t remainingTimeoutMs = timeoutMs - descriptorElapsedMs;
    const uint32_t soapAttemptTimeoutMs =
        cameraCredentialsConfigured() && remainingTimeoutMs > 500UL
            ? remainingTimeoutMs / 2UL
            : remainingTimeoutMs;

    if (!postOnvifSoap(
        mediaUrl,
        "http://www.onvif.org/ver10/media/wsdl/GetStreamUri",
        requestBody,
        streamXml,
        true,
        &statusCode,
        nullptr,
        soapAttemptTimeoutMs
    )) {
        error = String("GetStreamUri falhou; HTTP ") + String(statusCode);
        return false;
    }

    String returnedUri;
    int ignored = 0;

    if (!extractLocalTag(streamXml, "Uri", 0, returnedUri, ignored)) {
        error = "Resposta GetStreamUri sem URI RTSP";
        return false;
    }

    returnedUri = xmlUnescape(returnedUri);
    streamUri = normalizeLiveRtspUri(
        returnedUri,
        cameraIp,
        cameraRtspPort
    );

    if (streamUri.isEmpty()) {
        error = "A camera nao retornou uma URI RTSP valida";
        return false;
    }

    if (streamUri.length() > LIVE_STREAM_URI_MAX_LENGTH) {
        error = "A URI RTSP excedeu o limite do relay";
        return false;
    }

    if (takeLiveCatalogMutex(0)) {
        if (
            profileIndex < liveProfileCount &&
            liveProfiles[profileIndex].valid &&
            liveProfiles[profileIndex].token == profile.token &&
            liveCameraIp == cameraIp
        ) {
            liveProfiles[profileIndex].streamUri = streamUri;
            liveCatalogResult = String("URI RTSP pronta para o perfil ") +
                profile.name;
            liveCatalogUpdatedAtMs = millis();
        }

        giveLiveCatalogMutex();
    }

    populateLiveStreamDescriptor(profile, streamUri, descriptor);
    return true;
}

void appendLiveUnsigned64(String& output, uint64_t value)
{
    char buffer[24];
    snprintf(
        buffer,
        sizeof(buffer),
        "%llu",
        static_cast<unsigned long long>(value)
    );
    output += buffer;
}

void appendLiveStatusJson(String& json)
{
    struct LiveProfileStatusSnapshot {
        char name[LIVE_STREAM_PROFILE_NAME_MAX_LENGTH + 1] = {0};
        char encoding[LIVE_STREAM_ENCODING_MAX_LENGTH + 1] = {0};
        uint16_t width = 0;
        uint16_t height = 0;
        uint16_t frameRate = 0;
        uint32_t bitrateKbps = 0;
        bool preferred = false;
        bool relayCompatible = false;
        bool uriCached = false;
    };

    LiveRelayStats relayStats;
    liveStreamRelay.snapshotStats(relayStats);
    const unsigned long mediaPacketAgeMs = relayStats.lastMediaPacketAtMs == 0
        ? 0
        : millis() - relayStats.lastMediaPacketAtMs;
    const bool mediaRecentlyDelivered =
        relayStats.lastMediaPacketAtMs != 0 &&
        mediaPacketAgeMs <= LIVE_MEDIA_HEALTH_EVIDENCE_MS;

    LiveProfileStatusSnapshot profiles[MAX_LIVE_PROFILES];
    size_t profileCount = 0;
    bool catalogReady = false;
    String catalogResult;
    unsigned long catalogUpdatedAt = 0;

    if (takeLiveCatalogMutex()) {
        profileCount = min(liveProfileCount, MAX_LIVE_PROFILES);
        catalogReady = liveCatalogReady;
        catalogResult = liveCatalogResult;
        catalogUpdatedAt = liveCatalogUpdatedAtMs;

        for (size_t index = 0; index < profileCount; index++) {
            const LiveProfile& source = liveProfiles[index];
            LiveProfileStatusSnapshot& target = profiles[index];
            copySettingText(target.name, source.name);
            copySettingText(target.encoding, source.encoding);
            target.width = source.width;
            target.height = source.height;
            target.frameRate = source.frameRate;
            target.bitrateKbps = source.bitrateKbps;
            target.preferred = source.preferred;
            target.relayCompatible = liveEncodingCompatible(source.encoding);
            target.uriCached = !source.streamUri.isEmpty();
        }

        giveLiveCatalogMutex();
    } else {
        catalogResult = "Catalogo ocupado";
    }

    json += "\"live\":{";
    json += "\"available\":";
    json += catalogReady ? "true" : "false";
    json += ",\"streamPort\":";
    json += LIVE_STREAM_PORT;
    json += ",\"transport\":\"http-rtp\"";
    json += ",\"player\":\"mse-fmp4-rtp-compat-v2\"";
    json += ",\"catalogResult\":\"";
    json += jsonEscape(catalogResult);
    json += "\",\"catalogAgeSeconds\":";
    json += catalogUpdatedAt == 0
        ? 0UL
        : (millis() - catalogUpdatedAt) / 1000UL;
    json += ",\"relay\":{";
    json += "\"started\":";
    json += liveRelayStarted ? "true" : "false";
    json += ",\"running\":";
    json += relayStats.running ? "true" : "false";
    json += ",\"clientActive\":";
    json += relayStats.clientActive ? "true" : "false";
    json += ",\"mediaActive\":";
    json += relayStats.mediaActive ? "true" : "false";
    json += ",\"mediaRecentlyDelivered\":";
    json += mediaRecentlyDelivered ? "true" : "false";
    json += ",\"lastMediaPacketAgeSeconds\":";
    json += relayStats.lastMediaPacketAtMs == 0
        ? 0UL
        : mediaPacketAgeMs / 1000UL;
    json += ",\"port\":";
    json += relayStats.port;
    json += ",\"sessions\":";
    json += relayStats.sessions;
    json += ",\"failedSessions\":";
    json += relayStats.failedSessions;
    json += ",\"syncAttempts\":";
    json += relayStats.syncAttempts;
    json += ",\"syncAccepted\":";
    json += relayStats.syncAccepted;
    json += ",\"syncFailed\":";
    json += relayStats.syncFailed;
    json += ",\"syncSkipped\":";
    json += relayStats.syncSkipped;
    json += ",\"lastSyncAtMs\":";
    json += relayStats.lastSyncAtMs;
    json += ",\"lastSyncAgeSeconds\":";
    json += relayStats.lastSyncAtMs == 0
        ? 0UL
        : (millis() - relayStats.lastSyncAtMs) / 1000UL;
    json += ",\"lastSyncAccepted\":";
    json += relayStats.lastSyncAccepted ? "true" : "false";
    json += ",\"lastSyncResult\":\"";
    json += jsonEscape(relayStats.lastSyncResult);
    json += '\"';
    json += ",\"packetsRelayed\":";
    json += relayStats.packetsRelayed;
    json += ",\"bytesRelayed\":";
    appendLiveUnsigned64(json, relayStats.bytesRelayed);
    json += ",\"oversizedPackets\":";
    json += relayStats.oversizedPackets;
    json += ",\"lastSessionDurationSeconds\":";
    json += relayStats.lastSessionDurationSeconds;
    json += ",\"lastError\":\"";
    json += jsonEscape(relayStats.lastError);
    json += "\",\"startResult\":\"";
    json += jsonEscape(liveRelayStartResult);
    json += "\"},\"profiles\":[";

    for (size_t index = 0; index < profileCount; index++) {
        if (index > 0) {
            json += ',';
        }

        const LiveProfileStatusSnapshot& profile = profiles[index];
        json += "{\"index\":";
        json += index;
        json += ",\"name\":\"";
        json += jsonEscape(profile.name);
        json += "\",\"encoding\":\"";
        json += jsonEscape(profile.encoding);
        json += "\",\"width\":";
        json += profile.width;
        json += ",\"height\":";
        json += profile.height;
        json += ",\"fps\":";
        json += profile.frameRate;
        json += ",\"bitrateKbps\":";
        json += profile.bitrateKbps;
        json += ",\"preferred\":";
        json += profile.preferred ? "true" : "false";
        json += ",\"relayCompatible\":";
        json += profile.relayCompatible ? "true" : "false";
        json += ",\"uriCached\":";
        json += profile.uriCached ? "true" : "false";
        json += '}';
    }

    json += "]},";
}

void configureLiveRelay()
{
    if (liveRelayStarted) {
        return;
    }

    if (!ensureLiveCatalogMutex()) {
        liveRelayStartResult = "Heap insuficiente para criar o mutex do catalogo ao vivo";
        Serial.println(liveRelayStartResult);
        return;
    }

    if (!ensureCameraIoMutex()) {
        liveRelayStartResult = "Heap insuficiente para criar o mutex de I/O da camera";
        Serial.println(liveRelayStartResult);
        return;
    }

    snprintf(
        liveRelayUserAgent,
        sizeof(liveRelayUserAgent),
        "ESP32-Camera-Watchdog/%s",
        FIRMWARE_VERSION
    );

    liveRelayStarted = liveStreamRelay.begin(
        LIVE_STREAM_PORT,
        provideLiveStreamDescriptor,
        requestLiveSynchronizationPoint,
        cameraIoMutex,
        CAMERA_RTSP_USERNAME_VALUE,
        CAMERA_RTSP_PASSWORD_VALUE,
        liveRelayUserAgent
    );

    liveRelayStartResult = liveRelayStarted
        ? String("Relay HTTP iniciado na porta ") + String(LIVE_STREAM_PORT)
        : "Nao foi possivel criar a tarefa do relay ao vivo";

    Serial.println(liveRelayStartResult);
}
