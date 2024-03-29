/*
 * Copyright 2017-2019 Amazon.com, Inc. or its affiliates. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License").
 * You may not use this file except in compliance with the License.
 * A copy of the License is located at
 *
 *     http://aws.amazon.com/apache2.0/
 *
 * or in the "license" file accompanying this file. This file is distributed
 * on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either
 * express or implied. See the License for the specific language governing
 * permissions and limitations under the License.
 */

/// @file AudioPlayerTest.cpp

#include <chrono>
#include <future>
#include <memory>
#include <map>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <rapidjson/document.h>
#include <rapidjson/error/en.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

#include <AVSCommon/AVS/Attachment/AttachmentManager.h>
#include <AVSCommon/AVS/Attachment/AttachmentManagerInterface.h>
#include <AVSCommon/SDKInterfaces/MockExceptionEncounteredSender.h>
#include <AVSCommon/SDKInterfaces/MockContextManager.h>
#include <AVSCommon/SDKInterfaces/MockDirectiveSequencer.h>
#include <AVSCommon/SDKInterfaces/MockDirectiveHandlerResult.h>
#include <AVSCommon/SDKInterfaces/MockFocusManager.h>
#include <AVSCommon/SDKInterfaces/MockMessageSender.h>
#include <AVSCommon/SDKInterfaces/MockPlaybackRouter.h>
#include <AVSCommon/Utils/JSON/JSONUtils.h>
#include <AVSCommon/Utils/Logger/ConsoleLogger.h>
#include <AVSCommon/Utils/MediaPlayer/MockMediaPlayer.h>
#include <AVSCommon/Utils/MediaPlayer/PooledMediaPlayerFactory.h>
#include <AVSCommon/Utils/Memory/Memory.h>
#include <AVSCommon/Utils/PromiseFuturePair.h>

#include "AudioPlayer/AudioPlayer.h"

namespace alexaClientSDK {
namespace capabilityAgents {
namespace audioPlayer {
namespace test {

using namespace avsCommon::utils::json;
using namespace avsCommon::utils;
using namespace avsCommon;
using namespace avsCommon::avs;
using namespace avsCommon::avs::attachment;
using namespace avsCommon::sdkInterfaces;
using namespace avsCommon::sdkInterfaces::test;
using namespace avsCommon::utils::mediaPlayer;
using namespace avsCommon::utils::memory;
using namespace avsCommon::utils::mediaPlayer::test;
using namespace ::testing;
using namespace rapidjson;

/// Plenty of time for a test to complete.
static std::chrono::milliseconds WAIT_TIMEOUT(1000);

// Delay to let events happen / threads catch up
static std::chrono::milliseconds EVENT_PROCESS_DELAY(20);

/// The name of the @c FocusManager channel used by the @c AudioPlayer.
static const std::string CHANNEL_NAME(avsCommon::sdkInterfaces::FocusManagerInterface::CONTENT_CHANNEL_NAME);

/// Namespace for AudioPlayer.
static const std::string NAMESPACE_AUDIO_PLAYER("AudioPlayer");

/// Namespace for Another AudioPlayer.
static const std::string NAMESPACE_AUDIO_PLAYER_2("AudioPlayer_2");

/// Name for AudioPlayer Play directive.
static const std::string NAME_PLAY("Play");

/// Name for AudioPlayer Stop directive.
static const std::string NAME_STOP("Stop");

/// Name for AudioPlayer ClearQueue directive.
static const std::string NAME_CLEARQUEUE("ClearQueue");

/// The @c NamespaceAndName to send to the @c ContextManager.
static const NamespaceAndName NAMESPACE_AND_NAME_PLAYBACK_STATE{NAMESPACE_AUDIO_PLAYER, "PlaybackState"};

/// Message Id for testing.
static const std::string MESSAGE_ID_TEST("MessageId_Test");

/// Another message Id for testing.
static const std::string MESSAGE_ID_TEST_2("MessageId_Test2");

/// Another message Id for testing.
static const std::string MESSAGE_ID_TEST_3("MessageId_Test3");

/// PlayRequestId for testing.
static const std::string PLAY_REQUEST_ID_TEST("PlayRequestId_Test");

/// Context ID for testing
static const std::string CONTEXT_ID_TEST("ContextId_Test");

/// Context ID for testing
static const std::string CONTEXT_ID_TEST_2("ContextId_Test2");

/// Context ID for testing
static const std::string CONTEXT_ID_TEST_3("ContextId_Test3");

/// Token for testing.
static const std::string TOKEN_TEST("Token_Test");

/// Previous token for testing.
static const std::string PREV_TOKEN_TEST("Prev_Token_Test");

/// Format of the audio.
static const std::string FORMAT_TEST("AUDIO_MPEG");

/// URL for testing.
static const std::string URL_TEST("cid:Test");

/// ENQUEUE playBehavior.
static const std::string NAME_ENQUEUE("ENQUEUE");

/// REPLACE_ALL playBehavior.
static const std::string NAME_REPLACE_ALL("REPLACE_ALL");

/// CLEAR_ALL clearBehavior.
static const std::string NAME_CLEAR_ALL("CLEAR_ALL");

/// audioItemId for testing.
static const std::string AUDIO_ITEM_ID_1("testID1");
static const std::string AUDIO_ITEM_ID_2("testID2");

/// The @c FINISHED state of the @c AudioPlayer.
static const std::string FINISHED_STATE("FINISHED");

/// The @c PLAYING state of the @c AudioPlayer
static const std::string PLAYING_STATE{"PLAYING"};

/// The @c IDLE state of the @c AudioPlayer
static const std::string IDLE_STATE{"IDLE"};

/// The offset in milliseconds returned by the mock media player.
static const long OFFSET_IN_MILLISECONDS_TEST{100};

/// ExpiryTime for testing. Needs to be in ISO 8601 format.
static const std::string EXPIRY_TEST("481516234248151623421088");

/// progressReportDelayInMilliseconds for testing.
static const long PROGRESS_REPORT_DELAY{200};

/// progressReportIntervalInMilliseconds for testing.
static const long PROGRESS_REPORT_INTERVAL{100};

/// The offset in milliseconds returned by the mock media player slightly before the progressReportDelayInMilliseconds.
static const long OFFSET_IN_MILLISECONDS_BEFORE_PROGRESS_REPORT_DELAY{PROGRESS_REPORT_DELAY - 1};

/// The offset in milliseconds returned by the mock media player slightly before the progressReportDelayInMilliseconds.
static const long OFFSET_IN_MILLISECONDS_AFTER_PROGRESS_REPORT_DELAY{PROGRESS_REPORT_DELAY + 1};

/// The offset in milliseconds returned by the mock media player slightly before the
/// progressReportIntervalInMilliseconds.
static const long OFFSET_IN_MILLISECONDS_BEFORE_PROGRESS_REPORT_INTERVAL{PROGRESS_REPORT_INTERVAL - 1};

/// The offset in milliseconds returned by the mock media player slightly before the
/// progressReportIntervalInMilliseconds.
static const long OFFSET_IN_MILLISECONDS_AFTER_PROGRESS_REPORT_INTERVAL{PROGRESS_REPORT_INTERVAL + 1};

/// The time that must elapse in order to get 2.5 interval periods
static const std::chrono::milliseconds TIME_FOR_TWO_AND_A_HALF_INTERVAL_PERIODS{
    std::chrono::milliseconds((2 * PROGRESS_REPORT_INTERVAL) + (PROGRESS_REPORT_INTERVAL / 2))};

/// Payloads for testing.
static std::string createEnqueuePayloadTest(long offsetInMilliseconds, const std::string& audioId = AUDIO_ITEM_ID_1) {
    // clang-format off
    const std::string ENQUEUE_PAYLOAD_TEST =
        "{"
            "\"playBehavior\":\"" + NAME_ENQUEUE + "\","
            "\"audioItem\": {"
                "\"audioItemId\":\"" + audioId + "\","
                "\"stream\": {"
                    "\"url\":\"" + URL_TEST + "\","
                    "\"streamFormat\":\"" + FORMAT_TEST + "\","
                    "\"offsetInMilliseconds\":" + std::to_string(offsetInMilliseconds) + ","
                    "\"expiryTime\":\"" + EXPIRY_TEST + "\","
                    "\"progressReport\": {"
                        "\"progressReportDelayInMilliseconds\":" + std::to_string(PROGRESS_REPORT_DELAY) + ","
                        "\"progressReportIntervalInMilliseconds\":" + std::to_string(PROGRESS_REPORT_INTERVAL) +
                    "},"
                    "\"token\":\"" + TOKEN_TEST + "\","
                    "\"expectedPreviousToken\":\"\""
                "}"
            "}"
        "}";
    // clang-format on

    return ENQUEUE_PAYLOAD_TEST;
}

// clang-format off
static const std::string REPLACE_ALL_PAYLOAD_TEST =
"{"
    "\"playBehavior\":\"" + NAME_REPLACE_ALL + "\","
    "\"audioItem\": {"
        "\"audioItemId\":\"" + AUDIO_ITEM_ID_2 + "\","
        "\"stream\": {"
            "\"url\":\"" + URL_TEST + "\","
            "\"streamFormat\":\"" + FORMAT_TEST + "\","
            "\"offsetInMilliseconds\":" + std::to_string(OFFSET_IN_MILLISECONDS_TEST) + ","
            "\"expiryTime\":\"" + EXPIRY_TEST + "\","
            "\"progressReport\": {"
                "\"progressReportDelayInMilliseconds\":" + std::to_string(PROGRESS_REPORT_DELAY) + ","
                "\"progressReportIntervalInMilliseconds\":" + std::to_string(PROGRESS_REPORT_INTERVAL) +
            "},"
            "\"token\":\"" + TOKEN_TEST + "\","
            "\"expectedPreviousToken\":\"\""
        "}"
    "}"
"}";
// clang-format on

/// Empty payload for testing.
static const std::string EMPTY_PAYLOAD_TEST = "{}";

/// CLEAR_ALL payload for testing.
// clang-format off
static const std::string CLEAR_ALL_PAYLOAD_TEST =
    "{"
        "\"clearBehavior\":\"" + NAME_CLEAR_ALL + "\""
    "}";
// clang-format on

/// Token JSON key.
static const std::string TOKEN_KEY = "token";

/// Offset JSON key.
static const std::string OFFSET_KEY = "offsetInMilliseconds";

/// Player activity JSON key.
static const std::string ACTIVITY_KEY = "playerActivity";

/// The expected state when the @c AudioPlayer is not handling any directive.
// clang-format off
static const std::string IDLE_STATE_TEST =
    "{"
        "\"token\":\"\","
        "\"offsetInMilliseconds\":" + std::to_string(0) + ","
        "\"playerActivity\":\"" + IDLE_STATE + "\""
    "}";
// clang-format on

/// Provide State Token for testing.
static const unsigned int PROVIDE_STATE_TOKEN_TEST{1};

/// JSON key for the event section of a message.
static const std::string MESSAGE_EVENT_KEY = "event";

/// JSON key for the header section of a message.
static const std::string MESSAGE_HEADER_KEY = "header";

/// JSON key for the name section of a message.
static const std::string MESSAGE_NAME_KEY = "name";

/// JSON key for the token section of a message.
static const std::string MESSAGE_TOKEN_KEY = "token";

/// JSON key for the payload section of a message.
static const std::string MESSAGE_PAYLOAD_KEY = "payload";

/// JSON key for the metadata section of a message.
static const std::string MESSAGE_METADATA_KEY = "metadata";

/// JSON key for "string" type field in metadata section of StreamMetadataExtracted event.
static const std::string MESSAGE_METADATA_STRING_KEY = "StringKey";

/// JSON value for "string" type field in metadata section of StreamMetadataExtracted event.
static const std::string MESSAGE_METADATA_STRING_VALUE = "StringValue";

/// JSON key for "uint" type field in metadata section of StreamMetadataExtracted event.
static const std::string MESSAGE_METADATA_UINT_KEY = "UintKey";

/// JSON value for "uint" type field in metadata section of StreamMetadataExtracted event.
static const std::string MESSAGE_METADATA_UINT_VALUE = "12345";

/// JSON key for "int" type field in metadata section of StreamMetadataExtracted event.
static const std::string MESSAGE_METADATA_INT_KEY = "IntKey";

/// JSON value for "int" type field in metadata section of StreamMetadataExtracted event.
static const std::string MESSAGE_METADATA_INT_VALUE = "67890";

/// JSON key for "double" type field in metadata section of StreamMetadataExtracted event.
static const std::string MESSAGE_METADATA_DOUBLE_KEY = "DoubleKey";

/// JSON value for "double" type field in metadata section of StreamMetadataExtracted event.
static const std::string MESSAGE_METADATA_DOUBLE_VALUE = "3.14";

/// JSON key for "boolean" type field in metadata section of StreamMetadataExtracted event.
static const std::string MESSAGE_METADATA_BOOLEAN_KEY = "BooleanKey";

/// JSON value for "boolean" type field in metadata section of StreamMetadataExtracted event.
static const std::string MESSAGE_METADATA_BOOLEAN_VALUE = "true";

/// Name of PlaybackStarted event
static const std::string PLAYBACK_STARTED_NAME = "PlaybackStarted";

/// Name of PlaybackNearlyFinished event
static const std::string PLAYBACK_NEARLY_FINISHED_NAME = "PlaybackNearlyFinished";

/// Name of PlaybackFinished event
static const std::string PLAYBACK_FINISHED_NAME = "PlaybackFinished";

/// Name of PlaybackStopped event
static const std::string PLAYBACK_STOPPED_NAME = "PlaybackStopped";

/// Name of PlaybackPaused event
static const std::string PLAYBACK_PAUSED_NAME = "PlaybackPaused";

/// Name of PlaybackFailed event
static const std::string PLAYBACK_FAILED_NAME = "PlaybackFailed";

/// Name of PlaybackResumed event
static const std::string PLAYBACK_RESUMED_NAME = "PlaybackResumed";

/// Name of PlaybackStutterStarted event
static const std::string PLAYBACK_STUTTER_STARTED_NAME = "PlaybackStutterStarted";

/// Name of PlaybackStutterFinished event
static const std::string PLAYBACK_STUTTER_FINISHED_NAME = "PlaybackStutterFinished";

/// Name of ProgressReportDelayElapsed event
static const std::string PROGRESS_REPORT_DELAY_ELAPSED_NAME = "ProgressReportDelayElapsed";

/// Name of ProgressReportIntervalElapsed event
static const std::string PROGRESS_REPORT_INTERVAL_ELAPSED_NAME = "ProgressReportIntervalElapsed";

/// Name of StreamMetadataExtracted event
static const std::string STREAM_METADATA_EXTRACTED_NAME = "StreamMetadataExtracted";

/// String to identify log entries originating from this file.
static const std::string TAG("AudioPlayerTest");

/**
 * Create a LogEntry using this file's TAG and the specified event string.
 *
 * @param The event string for this @c LogEntry.
 */
#define LX(event) alexaClientSDK::avsCommon::utils::logger::LogEntry(TAG, event)

class TestAudioPlayerObserver : public AudioPlayerObserverInterface {
public:
    TestAudioPlayerObserver() : m_state{PlayerActivity::IDLE} {
    }
    bool waitFor(PlayerActivity activity, std::chrono::milliseconds timeout) {
        std::unique_lock<std::mutex> lock(m_mutex);
        return m_conditionVariable.wait_for(lock, timeout, [this, activity] { return m_state == activity; });
    }
    void onPlayerActivityChanged(avsCommon::avs::PlayerActivity state, const Context& context) override {
        ACSDK_DEBUG(
            LX("onPlayerActivityChanged")
                .d("state", state)
                .d("audioItemId", context.audioItemId)
                .d("offsetInMs", std::chrono::duration_cast<std::chrono::milliseconds>(context.offset).count()));
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_state = state;
        }
        m_conditionVariable.notify_all();
    }

private:
    PlayerActivity m_state;
    std::mutex m_mutex;
    std::condition_variable m_conditionVariable;
};

class AudioPlayerTest : public ::testing::Test {
public:
    AudioPlayerTest();

    void SetUp() override;
    void TearDown() override;
    void reSetUp(int numberOfPlayers);

    /// @c MediaPlayerFactory to generate MockMediaPlayers for testing;
    std::unique_ptr<alexaClientSDK::mediaPlayer::PooledMediaPlayerFactory> m_mockFactory;

    /// @c AudioPlayer to test
    std::shared_ptr<AudioPlayer> m_audioPlayer;

    /// @c A test observer to wait for @c AudioPlayer state changes
    std::shared_ptr<TestAudioPlayerObserver> m_testAudioPlayerObserver;

    /// Player to send the audio to.
    std::shared_ptr<MockMediaPlayer> m_mockMediaPlayer;

    /// Another Player to send the audio to.
    std::shared_ptr<MockMediaPlayer> m_mockMediaPlayerTrack2;

    /// Another Player to send the audio to.
    std::shared_ptr<MockMediaPlayer> m_mockMediaPlayerTrack3;

    /// @c ContextManager to provide state and update state.
    std::shared_ptr<MockContextManager> m_mockContextManager;

    /// @c FocusManager to request focus to the DIALOG channel.
    std::shared_ptr<MockFocusManager> m_mockFocusManager;

    /// A directive handler result to send the result to.
    std::unique_ptr<MockDirectiveHandlerResult> m_mockDirectiveHandlerResult;

    /// A message sender used to send events to AVS.
    std::shared_ptr<MockMessageSender> m_mockMessageSender;

    /// An exception sender used to send exception encountered events to AVS.
    std::shared_ptr<MockExceptionEncounteredSender> m_mockExceptionSender;

    /// A playback router to notify when @c AudioPlayer becomes active.
    std::shared_ptr<MockPlaybackRouter> m_mockPlaybackRouter;

    /// Attachment manager used to create a reader.
    std::shared_ptr<AttachmentManager> m_attachmentManager;

    /// Map for expected messages testing
    std::map<std::string, int> m_expectedMessages;

    /// Identifier for the currently selected audio source.
    MediaPlayerInterface::SourceId m_sourceId;

    /**
     * This is invoked in response to a @c setState call.
     *
     * @return @c SUCCESS.
     */
    SetStateResult wakeOnSetState();

    /// Promise to be fulfilled when @c setState is called.
    std::promise<void> m_wakeSetStatePromise;

    /// Future to notify when @c setState is called.
    std::future<void> m_wakeSetStateFuture;

    /**
     * This is invoked in response to a @c acquireChannel call.
     *
     * @return @c true
     */

    bool wakeOnAcquireChannel();

    /// Promise to be fulfilled when @c acquireChannel is called.
    std::promise<void> m_wakeAcquireChannelPromise;

    /// Future to notify when @c acquireChannel is called.
    std::future<void> m_wakeAcquireChannelFuture;

    /**
     * This is invoked in response to a @c releaseChannel call.
     *
     * @return @c true
     */
    std::future<bool> wakeOnReleaseChannel();

    /// Promise to be fulfilled when @c acquireChannel is called.
    std::promise<void> m_wakeReleaseChannelPromise;

    /// Future to notify when @c releaseChannel is called.
    std::future<void> m_wakeReleaseChannelFuture;

    /**
     * Fulfills the @c m_wakeSendMessagePromise. This is invoked in response to a @c sendMessage call.
     */
    void wakeOnSendMessage();

    /// Promise to be fulfilled when @c sendMessage is called.
    std::promise<void> m_wakeSendMessagePromise;

    /// Future to notify when @c sendMessage is called.
    std::future<void> m_wakeSendMessageFuture;

    /**
     * Consolidate code to send Play directive.
     *
     * @param offsetInMilliseconds The offset to use in the directive.
     */
    void sendPlayDirective(long offsetInMilliseconds = OFFSET_IN_MILLISECONDS_TEST);

    /**
     * Consolidate code to send Stop directive.
     */
    void sendStopDirective();

    /**
     * Consolidate code to send ClearQueue directive
     */

    void sendClearQueueDirective();

    /**
     * Verify that the message name matches the expected name
     *
     * @param request The @c MessageRequest to verify
     * @param expectedName The expected name to find in the json header
     */

    bool verifyMessage(std::shared_ptr<avsCommon::avs::MessageRequest> request, std::string expectedName);

    /**
     * Verify that the sent request matches one in a Map of expectedMessages
     *
     * @param request The @c MessageRequest to verify
     * @param expectedMessages The map of expected messages and a count of how many are seen
     */

    void verifyMessageMap(
        std::shared_ptr<avsCommon::avs::MessageRequest> request,
        std::map<std::string, int>* expectedMessages);

    /**
     * verify that the sent request matches the indexed message in the list
     *
     * @param orderedMessageList The list of expected messages, in order expected
     * @param index The expected message to match
     */
    void verifyMessageOrder(
        const std::vector<std::string>& orderedMessageList,
        int index,
        std::function<void()> trigger);

    /**
     * Verify that the provided state matches the expected state
     *
     * @param jsonState The state to verify
     * @param expectedState The expected state
     */

    void verifyState(const std::string& providedState, const std::string& expectedState);

    /**
     * Verify that the message name matches the expected name and also verify expected tags.
     *
     * @param request The @c MessageRequest to verify
     * @param expectedMessages Map of expected tags and count of how many are seen.
     */
    void verifyTags(
        std::shared_ptr<avsCommon::avs::MessageRequest> request,
        std::map<std::string, int>* expectedMessages);

    /**
     * Run through test of playing, enqueuing, finish, play
     */
    void testPlayEnqueFinishPlay();

    /// General purpose mutex.
    std::mutex m_mutex;

    /// Condition variable to wake on a message being sent.
    std::condition_variable m_messageSentTrigger;

    /// Condition variable to wake on MediaPlayer calls. This is used when a MediaPlayer call is expected to occur
    /// without a corresponding change in PlayerActivity.
    std::condition_variable m_mediaPlayerCallTrigger;
};

AudioPlayerTest::AudioPlayerTest() :
        m_wakeSetStatePromise{},
        m_wakeSetStateFuture{m_wakeSetStatePromise.get_future()},
        m_wakeAcquireChannelPromise{},
        m_wakeAcquireChannelFuture{m_wakeAcquireChannelPromise.get_future()},
        m_wakeReleaseChannelPromise{},
        m_wakeReleaseChannelFuture{m_wakeReleaseChannelPromise.get_future()},
        m_wakeSendMessagePromise{},
        m_wakeSendMessageFuture{m_wakeSendMessagePromise.get_future()} {
}

void AudioPlayerTest::SetUp() {
    MockMediaPlayer::enableConcurrentMediaPlayers();

    m_mockContextManager = std::make_shared<NiceMock<MockContextManager>>();
    m_mockFocusManager = std::make_shared<NiceMock<MockFocusManager>>();
    m_mockMessageSender = std::make_shared<NiceMock<MockMessageSender>>();
    m_mockExceptionSender = std::make_shared<NiceMock<MockExceptionEncounteredSender>>();
    m_attachmentManager = std::make_shared<AttachmentManager>(AttachmentManager::AttachmentType::IN_PROCESS);
    m_mockMediaPlayer = MockMediaPlayer::create();
    m_mockPlaybackRouter = std::make_shared<NiceMock<MockPlaybackRouter>>();
    ASSERT_TRUE(m_mockMediaPlayer);
    m_mockMediaPlayerTrack2 = MockMediaPlayer::create();
    ASSERT_TRUE(m_mockMediaPlayerTrack2);
    m_mockMediaPlayerTrack3 = MockMediaPlayer::create();
    ASSERT_TRUE(m_mockMediaPlayerTrack3);
    std::vector<std::shared_ptr<MediaPlayerInterface>> pool = {
        m_mockMediaPlayer, m_mockMediaPlayerTrack2, m_mockMediaPlayerTrack3};
    m_mockFactory = alexaClientSDK::mediaPlayer::PooledMediaPlayerFactory::create(pool);
    m_audioPlayer = AudioPlayer::create(
        std::move(m_mockFactory),
        m_mockMessageSender,
        m_mockFocusManager,
        m_mockContextManager,
        m_mockExceptionSender,
        m_mockPlaybackRouter);

    ASSERT_TRUE(m_audioPlayer);

    m_testAudioPlayerObserver = std::make_shared<TestAudioPlayerObserver>();
    m_audioPlayer->addObserver(m_testAudioPlayerObserver);
    m_mockDirectiveHandlerResult = std::unique_ptr<MockDirectiveHandlerResult>(new MockDirectiveHandlerResult);
}

void AudioPlayerTest::TearDown() {
    if (m_audioPlayer) {
        m_audioPlayer->shutdown();
    }
    m_mockMediaPlayer->shutdown();
    m_mockMediaPlayerTrack2->shutdown();
    m_mockMediaPlayerTrack3->shutdown();
}

void AudioPlayerTest::reSetUp(int numberOfPlayers) {
    ASSERT_LE(numberOfPlayers, 3);
    ASSERT_GE(numberOfPlayers, 1);
    if (m_audioPlayer) {
        m_audioPlayer->shutdown();
    }

    std::vector<std::shared_ptr<MediaPlayerInterface>> pool;
    switch (numberOfPlayers) {
        case 3:
            pool.push_back(m_mockMediaPlayerTrack3);
        case 2:
            pool.push_back(m_mockMediaPlayerTrack2);
        case 1:
            pool.push_back(m_mockMediaPlayer);
    }
    m_mockFactory = alexaClientSDK::mediaPlayer::PooledMediaPlayerFactory::create(pool);
    m_audioPlayer = AudioPlayer::create(
        std::move(m_mockFactory),
        m_mockMessageSender,
        m_mockFocusManager,
        m_mockContextManager,
        m_mockExceptionSender,
        m_mockPlaybackRouter);

    ASSERT_TRUE(m_audioPlayer);

    m_testAudioPlayerObserver = std::make_shared<TestAudioPlayerObserver>();
    m_audioPlayer->addObserver(m_testAudioPlayerObserver);
}

SetStateResult AudioPlayerTest::wakeOnSetState() {
    m_wakeSetStatePromise.set_value();
    return SetStateResult::SUCCESS;
}

bool AudioPlayerTest::wakeOnAcquireChannel() {
    m_wakeAcquireChannelPromise.set_value();
    return true;
}

std::future<bool> AudioPlayerTest::wakeOnReleaseChannel() {
    std::promise<bool> releaseChannelSuccess;
    std::future<bool> returnValue = releaseChannelSuccess.get_future();
    releaseChannelSuccess.set_value(true);
    m_wakeReleaseChannelPromise.set_value();
    return returnValue;
}

void AudioPlayerTest::wakeOnSendMessage() {
    m_wakeSendMessagePromise.set_value();
}

void AudioPlayerTest::sendPlayDirective(long offsetInMilliseconds) {
    auto avsMessageHeader =
        std::make_shared<AVSMessageHeader>(NAMESPACE_AUDIO_PLAYER, NAME_PLAY, MESSAGE_ID_TEST, PLAY_REQUEST_ID_TEST);

    std::shared_ptr<AVSDirective> playDirective = AVSDirective::create(
        "", avsMessageHeader, createEnqueuePayloadTest(offsetInMilliseconds), m_attachmentManager, CONTEXT_ID_TEST);
    EXPECT_CALL(*(m_mockFocusManager.get()), acquireChannel(CHANNEL_NAME, _, NAMESPACE_AUDIO_PLAYER))
        .Times(1)
        .WillOnce(InvokeWithoutArgs(this, &AudioPlayerTest::wakeOnAcquireChannel));

    EXPECT_CALL(*m_mockDirectiveHandlerResult, setCompleted());

    m_audioPlayer->CapabilityAgent::preHandleDirective(playDirective, std::move(m_mockDirectiveHandlerResult));
    m_audioPlayer->CapabilityAgent::handleDirective(MESSAGE_ID_TEST);

    ASSERT_EQ(std::future_status::ready, m_wakeAcquireChannelFuture.wait_for(WAIT_TIMEOUT));

    m_audioPlayer->onFocusChanged(FocusState::FOREGROUND);

    ASSERT_TRUE(m_testAudioPlayerObserver->waitFor(PlayerActivity::PLAYING, WAIT_TIMEOUT));
}

void AudioPlayerTest::sendStopDirective() {
    auto avsStopMessageHeader =
        std::make_shared<AVSMessageHeader>(NAMESPACE_AUDIO_PLAYER, NAME_STOP, MESSAGE_ID_TEST, PLAY_REQUEST_ID_TEST);

    std::shared_ptr<AVSDirective> stopDirective =
        AVSDirective::create("", avsStopMessageHeader, EMPTY_PAYLOAD_TEST, m_attachmentManager, CONTEXT_ID_TEST);

    m_audioPlayer->CapabilityAgent::preHandleDirective(stopDirective, std::move(m_mockDirectiveHandlerResult));
    m_audioPlayer->CapabilityAgent::handleDirective(MESSAGE_ID_TEST);
}

void AudioPlayerTest::sendClearQueueDirective() {
    auto avsClearMessageHeader = std::make_shared<AVSMessageHeader>(
        NAMESPACE_AUDIO_PLAYER, NAME_CLEARQUEUE, MESSAGE_ID_TEST, PLAY_REQUEST_ID_TEST);

    std::shared_ptr<AVSDirective> clearQueueDirective =
        AVSDirective::create("", avsClearMessageHeader, CLEAR_ALL_PAYLOAD_TEST, m_attachmentManager, CONTEXT_ID_TEST);

    m_audioPlayer->CapabilityAgent::preHandleDirective(clearQueueDirective, std::move(m_mockDirectiveHandlerResult));
    m_audioPlayer->CapabilityAgent::handleDirective(MESSAGE_ID_TEST);
}

bool AudioPlayerTest::verifyMessage(std::shared_ptr<avsCommon::avs::MessageRequest> request, std::string expectedName) {
    rapidjson::Document document;
    document.Parse(request->getJsonContent().c_str());
    EXPECT_FALSE(document.HasParseError())
        << "rapidjson detected a parsing error at offset:" + std::to_string(document.GetErrorOffset()) +
               ", error message: " + GetParseError_En(document.GetParseError());

    auto event = document.FindMember(MESSAGE_EVENT_KEY);
    EXPECT_NE(event, document.MemberEnd());

    auto header = event->value.FindMember(MESSAGE_HEADER_KEY);
    EXPECT_NE(header, event->value.MemberEnd());

    std::string requestName;
    jsonUtils::retrieveValue(header->value, MESSAGE_NAME_KEY, &requestName);

    return (requestName == expectedName);
}

void AudioPlayerTest::verifyMessageMap(
    std::shared_ptr<avsCommon::avs::MessageRequest> request,
    std::map<std::string, int>* expectedMessages) {
    rapidjson::Document document;
    document.Parse(request->getJsonContent().c_str());
    EXPECT_FALSE(document.HasParseError())
        << "rapidjson detected a parsing error at offset:" + std::to_string(document.GetErrorOffset()) +
               ", error message: " + GetParseError_En(document.GetParseError());

    auto event = document.FindMember(MESSAGE_EVENT_KEY);
    EXPECT_NE(event, document.MemberEnd());

    auto header = event->value.FindMember(MESSAGE_HEADER_KEY);
    EXPECT_NE(header, event->value.MemberEnd());

    std::string requestName;
    jsonUtils::retrieveValue(header->value, MESSAGE_NAME_KEY, &requestName);

    if (expectedMessages->find(requestName) != expectedMessages->end()) {
        expectedMessages->at(requestName) = expectedMessages->at(requestName) + 1;
    }
}

void AudioPlayerTest::verifyState(const std::string& providedState, const std::string& expectedState) {
    rapidjson::Document providedStateParsed;
    providedStateParsed.Parse(providedState);

    rapidjson::Document expectedStateParsed;
    expectedStateParsed.Parse(expectedState);

    EXPECT_EQ(providedStateParsed, expectedStateParsed);
}

void AudioPlayerTest::verifyTags(
    std::shared_ptr<avsCommon::avs::MessageRequest> request,
    std::map<std::string, int>* expectedMessages) {
    rapidjson::Document document;
    document.Parse(request->getJsonContent().c_str());
    EXPECT_FALSE(document.HasParseError())
        << "rapidjson detected a parsing error at offset:" + std::to_string(document.GetErrorOffset()) +
               ", error message: " + GetParseError_En(document.GetParseError());

    auto event = document.FindMember(MESSAGE_EVENT_KEY);
    EXPECT_NE(event, document.MemberEnd());

    auto header = event->value.FindMember(MESSAGE_HEADER_KEY);
    EXPECT_NE(header, event->value.MemberEnd());

    std::string requestName;
    jsonUtils::retrieveValue(header->value, MESSAGE_NAME_KEY, &requestName);

    if (expectedMessages->find(requestName) != expectedMessages->end()) {
        expectedMessages->at(requestName) = expectedMessages->at(requestName) + 1;
    }

    auto payload = event->value.FindMember(MESSAGE_PAYLOAD_KEY);
    EXPECT_NE(payload, event->value.MemberEnd());

    auto metadata = payload->value.FindMember(MESSAGE_METADATA_KEY);
    EXPECT_NE(metadata, payload->value.MemberEnd());

    std::string metadata_string_value;
    jsonUtils::retrieveValue(metadata->value, MESSAGE_METADATA_STRING_KEY, &metadata_string_value);

    if (expectedMessages->find(metadata_string_value) != expectedMessages->end()) {
        expectedMessages->at(metadata_string_value) = expectedMessages->at(metadata_string_value) + 1;
    }

    std::string metadata_uint_value;
    jsonUtils::retrieveValue(metadata->value, MESSAGE_METADATA_UINT_KEY, &metadata_uint_value);

    if (expectedMessages->find(metadata_uint_value) != expectedMessages->end()) {
        expectedMessages->at(metadata_uint_value) = expectedMessages->at(metadata_uint_value) + 1;
    }

    std::string metadata_int_value;
    jsonUtils::retrieveValue(metadata->value, MESSAGE_METADATA_INT_KEY, &metadata_int_value);

    if (expectedMessages->find(metadata_int_value) != expectedMessages->end()) {
        expectedMessages->at(metadata_int_value) = expectedMessages->at(metadata_int_value) + 1;
    }

    std::string metadata_double_value;
    jsonUtils::retrieveValue(metadata->value, MESSAGE_METADATA_DOUBLE_KEY, &metadata_double_value);

    if (expectedMessages->find(metadata_double_value) != expectedMessages->end()) {
        expectedMessages->at(metadata_double_value) = expectedMessages->at(metadata_double_value) + 1;
    }

    bool metadata_boolean_value = false;
    jsonUtils::retrieveValue(metadata->value, MESSAGE_METADATA_BOOLEAN_KEY, &metadata_boolean_value);
    ASSERT_TRUE(metadata_boolean_value);
}

/**
 * Test create() with nullptrs
 */

TEST_F(AudioPlayerTest, test_createWithNullPointers) {
    std::shared_ptr<AudioPlayer> testAudioPlayer;
    std::vector<std::shared_ptr<MediaPlayerInterface>> pool = {
        m_mockMediaPlayer, m_mockMediaPlayerTrack2, m_mockMediaPlayerTrack3};

    testAudioPlayer = AudioPlayer::create(
        nullptr,
        m_mockMessageSender,
        m_mockFocusManager,
        m_mockContextManager,
        m_mockExceptionSender,
        m_mockPlaybackRouter);
    EXPECT_EQ(testAudioPlayer, nullptr);

    m_mockFactory = alexaClientSDK::mediaPlayer::PooledMediaPlayerFactory::create(pool);
    testAudioPlayer = AudioPlayer::create(
        std::move(m_mockFactory),
        nullptr,
        m_mockFocusManager,
        m_mockContextManager,
        m_mockExceptionSender,
        m_mockPlaybackRouter);
    EXPECT_EQ(testAudioPlayer, nullptr);

    m_mockFactory = alexaClientSDK::mediaPlayer::PooledMediaPlayerFactory::create(pool);
    testAudioPlayer = AudioPlayer::create(
        std::move(m_mockFactory),
        m_mockMessageSender,
        nullptr,
        m_mockContextManager,
        m_mockExceptionSender,
        m_mockPlaybackRouter);
    EXPECT_EQ(testAudioPlayer, nullptr);

    m_mockFactory = alexaClientSDK::mediaPlayer::PooledMediaPlayerFactory::create(pool);
    testAudioPlayer = AudioPlayer::create(
        std::move(m_mockFactory),
        m_mockMessageSender,
        m_mockFocusManager,
        nullptr,
        m_mockExceptionSender,
        m_mockPlaybackRouter);
    EXPECT_EQ(testAudioPlayer, nullptr);

    m_mockFactory = alexaClientSDK::mediaPlayer::PooledMediaPlayerFactory::create(pool);
    testAudioPlayer = AudioPlayer::create(
        std::move(m_mockFactory),
        m_mockMessageSender,
        m_mockFocusManager,
        m_mockContextManager,
        nullptr,
        m_mockPlaybackRouter);
    EXPECT_EQ(testAudioPlayer, nullptr);

    m_mockFactory = alexaClientSDK::mediaPlayer::PooledMediaPlayerFactory::create(pool);
    testAudioPlayer = AudioPlayer::create(
        std::move(m_mockFactory),
        m_mockMessageSender,
        m_mockFocusManager,
        m_mockContextManager,
        m_mockExceptionSender,
        nullptr);
    EXPECT_EQ(testAudioPlayer, nullptr);
}

/**
 * Test transition from Idle to Playing
 */

TEST_F(AudioPlayerTest, test_transitionFromIdleToPlaying) {
    EXPECT_CALL(*(m_mockMediaPlayer.get()), play(_)).Times(AtLeast(1));
    sendPlayDirective();
}

/**
 * Test transition from Playing to Stopped with Stop Directive
 */

TEST_F(AudioPlayerTest, test_transitionFromPlayingToStopped) {
    sendPlayDirective();

    EXPECT_CALL(*(m_mockMediaPlayer.get()), stop(_)).Times(AtLeast(1));

    // now send Stop directive
    sendStopDirective();
    ASSERT_TRUE(m_testAudioPlayerObserver->waitFor(PlayerActivity::STOPPED, WAIT_TIMEOUT));
}

/**
 * Test transition from Playing to Stopped with ClearQueue.CLEAR_ALL Directive
 */

TEST_F(AudioPlayerTest, test_transitionFromPlayingToStoppedWithClear) {
    sendPlayDirective();

    EXPECT_CALL(*(m_mockMediaPlayer.get()), stop(_)).Times(AtLeast(1));

    sendClearQueueDirective();

    ASSERT_TRUE(m_testAudioPlayerObserver->waitFor(PlayerActivity::STOPPED, WAIT_TIMEOUT));
}

/**
 * Test transition from Stopped to Playing after issuing second Play directive
 */

TEST_F(AudioPlayerTest, test_transitionFromStoppedToPlaying) {
    sendPlayDirective();

    EXPECT_CALL(*(m_mockMediaPlayer.get()), stop(_)).Times(AtLeast(1));
    sendClearQueueDirective();
    ASSERT_TRUE(m_testAudioPlayerObserver->waitFor(PlayerActivity::STOPPED, WAIT_TIMEOUT));
    m_audioPlayer->onFocusChanged(FocusState::NONE);

    // Verify previous media player unused
    EXPECT_CALL(*(m_mockMediaPlayer.get()), play(_)).Times(0);
    EXPECT_CALL(*(m_mockMediaPlayerTrack2.get()), play(_)).Times(AtLeast(1));
    EXPECT_CALL(*(m_mockFocusManager.get()), acquireChannel(CHANNEL_NAME, _, NAMESPACE_AUDIO_PLAYER))
        .Times(1)
        .WillOnce(Return(true));
    auto avsMessageHeader = std::make_shared<AVSMessageHeader>(NAMESPACE_AUDIO_PLAYER, NAME_PLAY, MESSAGE_ID_TEST_2);
    std::shared_ptr<AVSDirective> playDirective = AVSDirective::create(
        "",
        avsMessageHeader,
        createEnqueuePayloadTest(OFFSET_IN_MILLISECONDS_TEST, AUDIO_ITEM_ID_2),
        m_attachmentManager,
        CONTEXT_ID_TEST_2);
    m_audioPlayer->CapabilityAgent::preHandleDirective(playDirective, std::move(m_mockDirectiveHandlerResult));
    m_audioPlayer->CapabilityAgent::handleDirective(MESSAGE_ID_TEST_2);
    m_audioPlayer->onFocusChanged(FocusState::FOREGROUND);
    ASSERT_TRUE(m_testAudioPlayerObserver->waitFor(PlayerActivity::PLAYING, WAIT_TIMEOUT));
}

/**
 * Test transition from Stopped to Playing after issuing second Play directive, resuming
 */

TEST_F(AudioPlayerTest, testTransitionFromStoppedToResumePlaying) {
    // send a play directive
    sendPlayDirective();

    // send clear Queue directive
    EXPECT_CALL(*(m_mockMediaPlayer.get()), stop(_)).Times(AtLeast(1));
    sendClearQueueDirective();

    ASSERT_TRUE(m_testAudioPlayerObserver->waitFor(PlayerActivity::STOPPED, WAIT_TIMEOUT));
    m_audioPlayer->onFocusChanged(FocusState::NONE);

    // re-init attachemnt manager - resuming is not normally done with attachments...
    m_attachmentManager = std::make_shared<AttachmentManager>(AttachmentManager::AttachmentType::IN_PROCESS);
    m_mockMediaPlayer->resetWaitTimer();

    EXPECT_CALL(*(m_mockMediaPlayerTrack2.get()), play(_)).Times(1);
    EXPECT_CALL(*(m_mockFocusManager.get()), acquireChannel(CHANNEL_NAME, _, NAMESPACE_AUDIO_PLAYER))
        .Times(1)
        .WillOnce(Return(true));

    // Enqueue next track
    auto avsMessageHeader = std::make_shared<AVSMessageHeader>(NAMESPACE_AUDIO_PLAYER, NAME_PLAY, MESSAGE_ID_TEST_2);

    std::shared_ptr<AVSDirective> playDirective = AVSDirective::create(
        "",
        avsMessageHeader,
        createEnqueuePayloadTest(OFFSET_IN_MILLISECONDS_TEST),
        m_attachmentManager,
        CONTEXT_ID_TEST);
    m_audioPlayer->CapabilityAgent::preHandleDirective(playDirective, std::move(m_mockDirectiveHandlerResult));
    m_audioPlayer->CapabilityAgent::handleDirective(MESSAGE_ID_TEST_2);
    m_audioPlayer->onFocusChanged(FocusState::FOREGROUND);
    ASSERT_TRUE(m_testAudioPlayerObserver->waitFor(PlayerActivity::PLAYING, WAIT_TIMEOUT));
}

/**
 * Test transition to next track, when next track has been enqueued
 */

TEST_F(AudioPlayerTest, testTransitionFromPlayingToPlayingNextEnqueuedTrack) {
    // send a play directive
    sendPlayDirective();
    ASSERT_TRUE(m_testAudioPlayerObserver->waitFor(PlayerActivity::PLAYING, WAIT_TIMEOUT));

    std::this_thread::sleep_for(std::chrono::milliseconds(EVENT_PROCESS_DELAY));

    // Verify that the media player for the enqueued track is used
    EXPECT_CALL(*(m_mockMediaPlayer.get()), play(_)).Times(0);
    EXPECT_CALL(*(m_mockMediaPlayerTrack2.get()), play(_)).Times(AtLeast(1));
    EXPECT_CALL(*(m_mockMediaPlayerTrack3.get()), play(_)).Times(0);

    // Enqueue next track
    auto avsMessageHeader = std::make_shared<AVSMessageHeader>(NAMESPACE_AUDIO_PLAYER, NAME_PLAY, MESSAGE_ID_TEST_2);
    std::shared_ptr<AVSDirective> playDirective = AVSDirective::create(
        "",
        avsMessageHeader,
        createEnqueuePayloadTest(OFFSET_IN_MILLISECONDS_TEST, AUDIO_ITEM_ID_2),
        m_attachmentManager,
        CONTEXT_ID_TEST_2);

    m_audioPlayer->CapabilityAgent::preHandleDirective(playDirective, std::move(m_mockDirectiveHandlerResult));
    m_audioPlayer->CapabilityAgent::handleDirective(MESSAGE_ID_TEST_2);

    std::this_thread::sleep_for(std::chrono::milliseconds(EVENT_PROCESS_DELAY));

    // Now, send a REPLACE_ALL directive that has the same ID as the enqueued track
    avsMessageHeader = std::make_shared<AVSMessageHeader>(NAMESPACE_AUDIO_PLAYER, NAME_PLAY, MESSAGE_ID_TEST_3);
    playDirective =
        AVSDirective::create("", avsMessageHeader, REPLACE_ALL_PAYLOAD_TEST, m_attachmentManager, CONTEXT_ID_TEST_3);
    m_audioPlayer->CapabilityAgent::preHandleDirective(playDirective, std::move(m_mockDirectiveHandlerResult));
    m_audioPlayer->CapabilityAgent::handleDirective(MESSAGE_ID_TEST_3);

    ASSERT_TRUE(m_testAudioPlayerObserver->waitFor(PlayerActivity::STOPPED, WAIT_TIMEOUT));
    ASSERT_TRUE(m_testAudioPlayerObserver->waitFor(PlayerActivity::PLAYING, WAIT_TIMEOUT));
}

/**
 * Test transition from Playing to Paused when focus changes to Dialog channel
 */

TEST_F(AudioPlayerTest, test_transitionFromPlayingToPaused) {
    sendPlayDirective();

    EXPECT_CALL(*(m_mockMediaPlayer.get()), pause(_)).Times(AtLeast(1));

    // simulate focus change
    m_audioPlayer->onFocusChanged(FocusState::BACKGROUND);

    ASSERT_TRUE(m_testAudioPlayerObserver->waitFor(PlayerActivity::PAUSED, WAIT_TIMEOUT));
}

/**
 * Test transition from Paused to Stopped on ClearQueue.CLEAR_ALL directive
 */
TEST_F(AudioPlayerTest, test_transitionFromPausedToStopped) {
    sendPlayDirective();

    EXPECT_CALL(*(m_mockMediaPlayer.get()), stop(_)).Times(AtLeast(1));

    // simulate focus change in order to pause
    m_audioPlayer->onFocusChanged(FocusState::BACKGROUND);

    ASSERT_TRUE(m_testAudioPlayerObserver->waitFor(PlayerActivity::PAUSED, WAIT_TIMEOUT));

    sendClearQueueDirective();

    ASSERT_TRUE(m_testAudioPlayerObserver->waitFor(PlayerActivity::STOPPED, WAIT_TIMEOUT));
}

/**
 * Test transition from Paused to Playing after resume
 */

TEST_F(AudioPlayerTest, test_resumeAfterPaused) {
    sendPlayDirective();

    EXPECT_CALL(*(m_mockMediaPlayer.get()), stop(_)).Times(AtLeast(1));

    // simulate focus change in order to pause
    m_audioPlayer->onFocusChanged(FocusState::BACKGROUND);

    ASSERT_TRUE(m_testAudioPlayerObserver->waitFor(PlayerActivity::PAUSED, WAIT_TIMEOUT));

    EXPECT_CALL(*(m_mockMediaPlayer.get()), resume(_)).Times(AtLeast(1));

    m_audioPlayer->onFocusChanged(FocusState::FOREGROUND);

    ASSERT_TRUE(m_testAudioPlayerObserver->waitFor(PlayerActivity::PLAYING, WAIT_TIMEOUT));
}

/**
 * Test @c provideState while IDLE
 */

TEST_F(AudioPlayerTest, test_callingProvideStateWhenIdle) {
    EXPECT_CALL(
        *(m_mockContextManager.get()),
        setState(NAMESPACE_AND_NAME_PLAYBACK_STATE, _, StateRefreshPolicy::NEVER, PROVIDE_STATE_TOKEN_TEST))
        .Times(1)
        .WillOnce(DoAll(
            // need to include all four arguments, but only care about jsonState
            Invoke([this](
                       const avs::NamespaceAndName& namespaceAndName,
                       const std::string& jsonState,
                       const avs::StateRefreshPolicy& refreshPolicy,
                       const unsigned int stateRequestToken) { verifyState(jsonState, IDLE_STATE_TEST); }),
            InvokeWithoutArgs(this, &AudioPlayerTest::wakeOnSetState)));

    m_audioPlayer->provideState(NAMESPACE_AND_NAME_PLAYBACK_STATE, PROVIDE_STATE_TOKEN_TEST);
    ASSERT_TRUE(std::future_status::ready == m_wakeSetStateFuture.wait_for(WAIT_TIMEOUT));
}

/**
 * Test @c onPlaybackError and expect a PlaybackFailed message
 */

TEST_F(AudioPlayerTest, test_onPlaybackError) {
    m_expectedMessages.insert({PLAYBACK_STARTED_NAME, 0});
    m_expectedMessages.insert({PLAYBACK_FAILED_NAME, 0});
    m_expectedMessages.insert({PLAYBACK_STOPPED_NAME, 0});

    EXPECT_CALL(*(m_mockMessageSender.get()), sendMessage(_))
        .Times(AtLeast(1))
        .WillRepeatedly(Invoke([this](std::shared_ptr<avsCommon::avs::MessageRequest> request) {
            std::lock_guard<std::mutex> lock(m_mutex);
            verifyMessageMap(request, &m_expectedMessages);
            m_messageSentTrigger.notify_one();
        }));

    sendPlayDirective();

    m_audioPlayer->onPlaybackError(
        m_mockMediaPlayer->getCurrentSourceId(), ErrorType::MEDIA_ERROR_UNKNOWN, "TEST_ERROR");

    std::unique_lock<std::mutex> lock(m_mutex);

    bool result;

    result = m_messageSentTrigger.wait_for(lock, WAIT_TIMEOUT, [this] {
        for (auto messageStatus : m_expectedMessages) {
            if (messageStatus.second == 0) {
                return false;
            }
        }
        return true;
    });
    ASSERT_TRUE(m_testAudioPlayerObserver->waitFor(PlayerActivity::STOPPED, WAIT_TIMEOUT));
    ASSERT_TRUE(result);
}

/**
 * Test @c onPlaybackError and expect a PlaybackFailed message
 */

TEST_F(AudioPlayerTest, test_onPlaybackError_Stopped) {
    m_expectedMessages.insert({PLAYBACK_STARTED_NAME, 0});
    m_expectedMessages.insert({PLAYBACK_STOPPED_NAME, 0});
    // we don't want to see this, as it shouldn't be send when stopped
    m_expectedMessages.insert({PLAYBACK_FAILED_NAME, -1});

    EXPECT_CALL(*(m_mockMessageSender.get()), sendMessage(_))
        .Times(AtLeast(1))
        .WillRepeatedly(Invoke([this](std::shared_ptr<avsCommon::avs::MessageRequest> request) {
            std::lock_guard<std::mutex> lock(m_mutex);
            verifyMessageMap(request, &m_expectedMessages);
            m_messageSentTrigger.notify_one();
        }));

    sendPlayDirective();
    ASSERT_TRUE(m_testAudioPlayerObserver->waitFor(PlayerActivity::PLAYING, WAIT_TIMEOUT));

    sendStopDirective();
    ASSERT_TRUE(m_testAudioPlayerObserver->waitFor(PlayerActivity::STOPPED, WAIT_TIMEOUT));

    m_audioPlayer->onPlaybackError(
        m_mockMediaPlayer->getCurrentSourceId(), ErrorType::MEDIA_ERROR_UNKNOWN, "TEST_ERROR");

    std::unique_lock<std::mutex> lock(m_mutex);

    bool result;

    result = m_messageSentTrigger.wait_for(lock, WAIT_TIMEOUT, [this] {
        for (auto messageStatus : m_expectedMessages) {
            if (messageStatus.second == 0) {
                return false;
            }
        }
        return true;
    });
    ASSERT_TRUE(result);
}

/**
 * Test @c onPlaybackError during pre-buffering
 */

TEST_F(AudioPlayerTest, testPrebufferOnPlaybackError) {
    m_expectedMessages.insert({PLAYBACK_FAILED_NAME, 0});

    EXPECT_CALL(*(m_mockMessageSender.get()), sendMessage(_))
        .Times(AtLeast(1))
        .WillRepeatedly(Invoke([this](std::shared_ptr<avsCommon::avs::MessageRequest> request) {
            std::lock_guard<std::mutex> lock(m_mutex);
            verifyMessageMap(request, &m_expectedMessages);
            m_messageSentTrigger.notify_one();
        }));

    sendPlayDirective();
    ASSERT_TRUE(m_testAudioPlayerObserver->waitFor(PlayerActivity::PLAYING, WAIT_TIMEOUT));

    bool result;

    std::this_thread::sleep_for(std::chrono::milliseconds(EVENT_PROCESS_DELAY));

    // Verify that the media player for the enqueued track is used
    EXPECT_CALL(*(m_mockMediaPlayer.get()), play(_)).Times(0);
    // zero because we should error out before play is called
    EXPECT_CALL(*(m_mockMediaPlayerTrack2.get()), play(_)).Times(0);

    // Enqueue next track
    auto avsMessageHeader = std::make_shared<AVSMessageHeader>(NAMESPACE_AUDIO_PLAYER, NAME_PLAY, MESSAGE_ID_TEST_2);
    std::shared_ptr<AVSDirective> playDirective = AVSDirective::create(
        "",
        avsMessageHeader,
        createEnqueuePayloadTest(OFFSET_IN_MILLISECONDS_TEST, AUDIO_ITEM_ID_2),
        m_attachmentManager,
        CONTEXT_ID_TEST_2);
    m_audioPlayer->CapabilityAgent::preHandleDirective(playDirective, std::move(m_mockDirectiveHandlerResult));
    std::this_thread::sleep_for(std::chrono::milliseconds(EVENT_PROCESS_DELAY));

    // Send error for track 2 while track 1 is playing
    m_audioPlayer->onPlaybackError(
        m_mockMediaPlayerTrack2->getSourceId(), ErrorType::MEDIA_ERROR_UNKNOWN, "TEST_ERROR");

    // now 'play' track 2
    m_audioPlayer->CapabilityAgent::handleDirective(MESSAGE_ID_TEST_2);
    std::this_thread::sleep_for(std::chrono::milliseconds(EVENT_PROCESS_DELAY));

    {
        // verify error not sent
        std::unique_lock<std::mutex> lock(m_mutex);

        result = m_messageSentTrigger.wait_for(lock, WAIT_TIMEOUT, [this] {
            for (auto messageStatus : m_expectedMessages) {
                if (messageStatus.second == 0) {
                    return false;
                }
            }
            return true;
        });
        ASSERT_FALSE(result);
    }

    // verify error sent
    std::unique_lock<std::mutex> lock(m_mutex);
    // Second track enqueue, but had an error loading.  now advance by finishing playing
    m_audioPlayer->onPlaybackFinished(m_mockMediaPlayer->getSourceId());

    result = m_messageSentTrigger.wait_for(lock, WAIT_TIMEOUT, [this] {
        for (auto messageStatus : m_expectedMessages) {
            if (messageStatus.second == 0) {
                return false;
            }
        }
        return true;
    });
    ASSERT_TRUE(result);
}

/**
 * Test @c onPlaybackPaused and expect a PlaybackPaused message
 */

TEST_F(AudioPlayerTest, test_onPlaybackPaused) {
    m_expectedMessages.insert({PLAYBACK_STARTED_NAME, 0});
    m_expectedMessages.insert({PLAYBACK_PAUSED_NAME, 0});

    EXPECT_CALL(*(m_mockMessageSender.get()), sendMessage(_))
        .Times(AtLeast(1))
        .WillRepeatedly(Invoke([this](std::shared_ptr<avsCommon::avs::MessageRequest> request) {
            std::lock_guard<std::mutex> lock(m_mutex);
            verifyMessageMap(request, &m_expectedMessages);
            m_messageSentTrigger.notify_one();
        }));

    sendPlayDirective();

    m_audioPlayer->onFocusChanged(FocusState::BACKGROUND);

    ASSERT_TRUE(m_testAudioPlayerObserver->waitFor(PlayerActivity::PAUSED, WAIT_TIMEOUT));

    std::unique_lock<std::mutex> lock(m_mutex);

    bool result;

    result = m_messageSentTrigger.wait_for(lock, WAIT_TIMEOUT, [this] {
        for (auto messageStatus : m_expectedMessages) {
            if (messageStatus.second == 0) {
                return false;
            }
        }
        return true;
    });

    ASSERT_TRUE(result);
}

/**
 * Test @c onPlaybackResumed and expect a PlaybackResumed message
 */

TEST_F(AudioPlayerTest, test_onPlaybackResumed) {
    m_expectedMessages.insert({PLAYBACK_STARTED_NAME, 0});
    m_expectedMessages.insert({PLAYBACK_RESUMED_NAME, 0});

    EXPECT_CALL(*(m_mockMessageSender.get()), sendMessage(_))
        .Times(AtLeast(1))
        .WillRepeatedly(Invoke([this](std::shared_ptr<avsCommon::avs::MessageRequest> request) {
            std::lock_guard<std::mutex> lock(m_mutex);
            verifyMessageMap(request, &m_expectedMessages);
            m_messageSentTrigger.notify_one();
        }));

    sendPlayDirective();
    ASSERT_TRUE(m_testAudioPlayerObserver->waitFor(PlayerActivity::PLAYING, WAIT_TIMEOUT));

    bool result;

    m_audioPlayer->onPlaybackResumed(m_mockMediaPlayer->getCurrentSourceId());

    std::unique_lock<std::mutex> lock(m_mutex);

    result = m_messageSentTrigger.wait_for(lock, WAIT_TIMEOUT, [this] {
        for (auto messageStatus : m_expectedMessages) {
            if (messageStatus.second == 0) {
                return false;
            }
        }
        return true;
    });
    ASSERT_TRUE(result);
}

/**
 * Test @c onPlaybackFinished and expect a PLAYBACK_NEARLY_FINISHED_NAME and a PLAYBACK_FINISHED_NAME message
 */

TEST_F(AudioPlayerTest, test_onPlaybackFinished) {
    m_expectedMessages.insert({PLAYBACK_STARTED_NAME, 0});
    m_expectedMessages.insert({PLAYBACK_NEARLY_FINISHED_NAME, 0});
    m_expectedMessages.insert({PLAYBACK_FINISHED_NAME, 0});

    EXPECT_CALL(*(m_mockMessageSender.get()), sendMessage(_))
        .Times(AtLeast(1))
        .WillRepeatedly(Invoke([this](std::shared_ptr<avsCommon::avs::MessageRequest> request) {
            std::lock_guard<std::mutex> lock(m_mutex);
            verifyMessageMap(request, &m_expectedMessages);
            m_messageSentTrigger.notify_one();
        }));

    sendPlayDirective();
    ASSERT_TRUE(m_testAudioPlayerObserver->waitFor(PlayerActivity::PLAYING, WAIT_TIMEOUT));

    std::unique_lock<std::mutex> lock(m_mutex);

    m_audioPlayer->onPlaybackFinished(m_mockMediaPlayer->getCurrentSourceId());

    bool result;

    result = m_messageSentTrigger.wait_for(lock, WAIT_TIMEOUT, [this] {
        for (auto messageStatus : m_expectedMessages) {
            if (messageStatus.second == 0) {
                return false;
            }
        }
        return true;
    });

    ASSERT_TRUE(result);
}

/**
 * Test @c onBufferUnderrun and expect a PlaybackStutterStarted message
 */

TEST_F(AudioPlayerTest, test_onBufferUnderrun) {
    m_expectedMessages.insert({PLAYBACK_STARTED_NAME, 0});
    m_expectedMessages.insert({PLAYBACK_STUTTER_STARTED_NAME, 0});

    EXPECT_CALL(*(m_mockMessageSender.get()), sendMessage(_))
        .Times(AtLeast(1))
        .WillRepeatedly(Invoke([this](std::shared_ptr<avsCommon::avs::MessageRequest> request) {
            std::lock_guard<std::mutex> lock(m_mutex);
            verifyMessageMap(request, &m_expectedMessages);
            m_messageSentTrigger.notify_one();
        }));

    sendPlayDirective();

    m_audioPlayer->onBufferUnderrun(m_mockMediaPlayer->getCurrentSourceId());

    std::unique_lock<std::mutex> lock(m_mutex);

    bool result;

    result = m_messageSentTrigger.wait_for(lock, WAIT_TIMEOUT, [this] {
        for (auto messageStatus : m_expectedMessages) {
            if (messageStatus.second == 0) {
                return false;
            }
        }
        return true;
    });

    ASSERT_TRUE(result);
}

/**
 * Test @c onBufferRefilled and expect a PlaybackStutterFinished message
 */

TEST_F(AudioPlayerTest, testTimer_onBufferRefilled) {
    m_expectedMessages.insert({PLAYBACK_STARTED_NAME, 0});
    m_expectedMessages.insert({PLAYBACK_STUTTER_FINISHED_NAME, 0});

    EXPECT_CALL(*(m_mockMessageSender.get()), sendMessage(_))
        .Times(AtLeast(1))
        .WillRepeatedly(Invoke([this](std::shared_ptr<avsCommon::avs::MessageRequest> request) {
            std::lock_guard<std::mutex> lock(m_mutex);
            verifyMessageMap(request, &m_expectedMessages);
            m_messageSentTrigger.notify_one();
        }));

    sendPlayDirective();

    m_audioPlayer->onBufferRefilled(m_mockMediaPlayer->getCurrentSourceId());

    std::unique_lock<std::mutex> lock(m_mutex);

    bool result;

    result = m_messageSentTrigger.wait_for(lock, WAIT_TIMEOUT, [this] {
        for (auto messageStatus : m_expectedMessages) {
            if (messageStatus.second == 0) {
                return false;
            }
        }
        return true;
    });

    ASSERT_TRUE(result);
}

/**
 * Test @c onTags and expect valid JSON.
 * Build a vector of tags and pass to Observer (onTags).
 * Observer will use the vector of tags and build a valid JSON object
 * "StreamMetadataExtracted Event". This JSON object is verified in verifyTags.
 */

TEST_F(AudioPlayerTest, test_onTags) {
    m_expectedMessages.insert({STREAM_METADATA_EXTRACTED_NAME, 0});
    m_expectedMessages.insert({MESSAGE_METADATA_STRING_VALUE, 0});
    m_expectedMessages.insert({MESSAGE_METADATA_UINT_VALUE, 0});
    m_expectedMessages.insert({MESSAGE_METADATA_DOUBLE_VALUE, 0});

    EXPECT_CALL(*(m_mockMessageSender.get()), sendMessage(_))
        .Times(AtLeast(1))
        .WillRepeatedly(Invoke([this](std::shared_ptr<avsCommon::avs::MessageRequest> request) {
            if (!m_mockMediaPlayer->waitUntilPlaybackStopped(std::chrono::milliseconds(0))) {
                std::lock_guard<std::mutex> lock(m_mutex);
                verifyTags(request, &m_expectedMessages);
                m_messageSentTrigger.notify_one();
            }
        }));

    std::unique_ptr<AudioPlayer::VectorOfTags> ptrToVectorOfTags = make_unique<AudioPlayer::VectorOfTags>();
    auto vectorOfTags = ptrToVectorOfTags.get();

    // Populate vector with dummy tags
    AudioPlayer::TagKeyValueType stringTag, uintTag, intTag, doubleTag, booleanTag;
    stringTag.key = std::string(MESSAGE_METADATA_STRING_KEY);
    stringTag.value = std::string(MESSAGE_METADATA_STRING_VALUE);
    stringTag.type = AudioPlayer::TagType::STRING;
    vectorOfTags->push_back(stringTag);

    uintTag.key = std::string(MESSAGE_METADATA_UINT_KEY);
    uintTag.value = std::string(MESSAGE_METADATA_UINT_VALUE);
    uintTag.type = AudioPlayer::TagType::UINT;
    vectorOfTags->push_back(uintTag);

    intTag.key = std::string(MESSAGE_METADATA_INT_KEY);
    intTag.value = std::string(MESSAGE_METADATA_INT_VALUE);
    intTag.type = AudioPlayer::TagType::INT;
    vectorOfTags->push_back(intTag);

    doubleTag.key = std::string(MESSAGE_METADATA_DOUBLE_KEY);
    doubleTag.value = std::string(MESSAGE_METADATA_DOUBLE_VALUE);
    doubleTag.type = AudioPlayer::TagType::DOUBLE;
    vectorOfTags->push_back(doubleTag);

    booleanTag.key = std::string(MESSAGE_METADATA_BOOLEAN_KEY);
    booleanTag.value = std::string(MESSAGE_METADATA_BOOLEAN_VALUE);
    booleanTag.type = AudioPlayer::TagType::BOOLEAN;
    vectorOfTags->push_back(booleanTag);

    m_audioPlayer->onTags(m_mockMediaPlayer->getCurrentSourceId(), std::move(ptrToVectorOfTags));

    std::unique_lock<std::mutex> lock(m_mutex);

    auto result = m_messageSentTrigger.wait_for(lock, WAIT_TIMEOUT, [this] {
        for (auto messageStatus : m_expectedMessages) {
            if (messageStatus.second == 0) {
                return false;
            }
        }
        return true;
    });
    ASSERT_TRUE(result);
}

/**
 * Test @c cancelDirective
 * Expect the @c handleDirective call to the cancelled directive returns false
 */

TEST_F(AudioPlayerTest, test_cancelDirective) {
    sendPlayDirective();

    m_audioPlayer->CapabilityAgent::cancelDirective(MESSAGE_ID_TEST);

    ASSERT_FALSE(m_audioPlayer->CapabilityAgent::handleDirective(MESSAGE_ID_TEST));
}

/**
 * Test focus change to NONE in IDLE state
 * Expect nothing to happen
 */

TEST_F(AudioPlayerTest, test_focusChangeToNoneInIdleState) {
    // switching to FocusState::NONE should cause no change
    m_audioPlayer->onFocusChanged(FocusState::NONE);
    ASSERT_TRUE(m_testAudioPlayerObserver->waitFor(PlayerActivity::IDLE, WAIT_TIMEOUT));
}

/**
 * Test focus change from FOREGROUND to BACKGROUND in IDLE state
 * Expect a call to pause(). This call is intended to go through MediaPlayer and cause nothing to happen
 * due to a lack of a queued AudioItem.
 */

TEST_F(AudioPlayerTest, test_focusChangeFromForegroundToBackgroundInIdleState) {
    m_audioPlayer->onFocusChanged(FocusState::FOREGROUND);

    // ensure AudioPlayer is still IDLE
    ASSERT_TRUE(m_testAudioPlayerObserver->waitFor(PlayerActivity::IDLE, WAIT_TIMEOUT));

    m_audioPlayer->onFocusChanged(FocusState::BACKGROUND);
}

/**
 * Test focus change in from NONE to BACKGROUND while IDLE.
 * Expect a call to pause. This isn't an expected state during normal execution.
 */

TEST_F(AudioPlayerTest, test_focusChangeFromNoneToBackgroundInIdleState) {
    m_audioPlayer->onFocusChanged(FocusState::BACKGROUND);
}

/**
 * Test focus changes in PLAYING state
 * Expect to pause when switching to BACKGROUND and to stop when switching to NONE
 */

TEST_F(AudioPlayerTest, test_focusChangesInPlayingState) {
    sendPlayDirective();

    // already in FOREGROUND, expect no change
    m_audioPlayer->onFocusChanged(FocusState::FOREGROUND);
    ASSERT_TRUE(m_testAudioPlayerObserver->waitFor(PlayerActivity::PLAYING, WAIT_TIMEOUT));

    // expect to pause in BACKGROUND
    EXPECT_CALL(*(m_mockMediaPlayer.get()), pause(_)).Times(1);
    m_audioPlayer->onFocusChanged(FocusState::BACKGROUND);
    ASSERT_TRUE(m_testAudioPlayerObserver->waitFor(PlayerActivity::PAUSED, WAIT_TIMEOUT));

    // expect to resume when switching back to FOREGROUND
    EXPECT_CALL(*(m_mockMediaPlayer.get()), resume(_)).Times(1);
    m_audioPlayer->onFocusChanged(FocusState::FOREGROUND);
    ASSERT_TRUE(m_testAudioPlayerObserver->waitFor(PlayerActivity::PLAYING, WAIT_TIMEOUT));

    // expect to stop when changing focus to NONE
    EXPECT_CALL(*(m_mockMediaPlayer.get()), stop(_)).Times(1);
    m_audioPlayer->onFocusChanged(FocusState::NONE);
    ASSERT_TRUE(m_testAudioPlayerObserver->waitFor(PlayerActivity::STOPPED, WAIT_TIMEOUT));
}

/**
 * Test focus changes in STOPPED state
 * Expect to remain in STOPPED state when switching to FOREGROUND (because there are no queued AudioItems) and
 * to transition to PAUSED when switching to BACKGROUND.
 */

TEST_F(AudioPlayerTest, test_focusChangesInStoppedState) {
    sendPlayDirective();

    // push AudioPlayer into stopped state
    EXPECT_CALL(*(m_mockMediaPlayer.get()), stop(_)).Times(AtLeast(1));
    m_audioPlayer->onFocusChanged(FocusState::NONE);
    ASSERT_TRUE(m_testAudioPlayerObserver->waitFor(PlayerActivity::STOPPED, WAIT_TIMEOUT));

    m_audioPlayer->onFocusChanged(FocusState::FOREGROUND);
    ASSERT_TRUE(m_testAudioPlayerObserver->waitFor(PlayerActivity::STOPPED, WAIT_TIMEOUT));

    EXPECT_CALL(*(m_mockMediaPlayer.get()), pause(_)).Times(1);
    m_audioPlayer->onFocusChanged(FocusState::BACKGROUND);
    ASSERT_TRUE(m_testAudioPlayerObserver->waitFor(PlayerActivity::PAUSED, WAIT_TIMEOUT));
}

/**
 * Test focus changes in PAUSED state
 * Expect to resume when switching to FOREGROUND, expect nothing when switching to BACKGROUND, expect stop when
 * switching to NONE
 */
TEST_F(AudioPlayerTest, test_focusChangesInPausedState) {
    sendPlayDirective();

    // push AudioPlayer into paused state
    EXPECT_CALL(*(m_mockMediaPlayer.get()), pause(_)).Times(1);
    m_audioPlayer->onFocusChanged(FocusState::BACKGROUND);
    ASSERT_TRUE(m_testAudioPlayerObserver->waitFor(PlayerActivity::PAUSED, WAIT_TIMEOUT));

    // expect a resume when switching back to FOREGROUND
    EXPECT_CALL(*(m_mockMediaPlayer.get()), resume(_)).Times(1);
    m_audioPlayer->onFocusChanged(FocusState::FOREGROUND);
    ASSERT_TRUE(m_testAudioPlayerObserver->waitFor(PlayerActivity::PLAYING, WAIT_TIMEOUT));

    // return to paused state
    EXPECT_CALL(*(m_mockMediaPlayer.get()), pause(_)).Times(1);
    m_audioPlayer->onFocusChanged(FocusState::BACKGROUND);
    ASSERT_TRUE(m_testAudioPlayerObserver->waitFor(PlayerActivity::PAUSED, WAIT_TIMEOUT));

    // expect nothing to happen when switching to BACKGROUND from BACKGROUND
    m_audioPlayer->onFocusChanged(FocusState::BACKGROUND);
    ASSERT_TRUE(m_testAudioPlayerObserver->waitFor(PlayerActivity::PAUSED, WAIT_TIMEOUT));

    // expect stop when switching to NONE focus
    EXPECT_CALL(*(m_mockMediaPlayer.get()), stop(_)).Times(1);
    m_audioPlayer->onFocusChanged(FocusState::NONE);
    ASSERT_TRUE(m_testAudioPlayerObserver->waitFor(PlayerActivity::STOPPED, WAIT_TIMEOUT));
}

/**
 * Test focus changes in BUFFER_UNDERRUN state
 * Expect nothing to happen when switching to FOREGROUND, expect to pause when switching to BACKGROUND, expect to stop
 * when switching to NONE
 */

TEST_F(AudioPlayerTest, test_focusChangesInBufferUnderrunState) {
    sendPlayDirective();

    // push AudioPlayer into buffer underrun state
    m_audioPlayer->onBufferUnderrun(m_mockMediaPlayer->getCurrentSourceId());
    ASSERT_TRUE(m_testAudioPlayerObserver->waitFor(PlayerActivity::BUFFER_UNDERRUN, WAIT_TIMEOUT));

    // nothing happens, AudioPlayer already in FOREGROUND
    m_audioPlayer->onFocusChanged(FocusState::FOREGROUND);
    ASSERT_TRUE(m_testAudioPlayerObserver->waitFor(PlayerActivity::BUFFER_UNDERRUN, WAIT_TIMEOUT));

    // expect to pause if pushed to BACKGROUND
    EXPECT_CALL(*(m_mockMediaPlayer.get()), pause(_)).Times(1);
    m_audioPlayer->onFocusChanged(FocusState::BACKGROUND);
    ASSERT_TRUE(m_testAudioPlayerObserver->waitFor(PlayerActivity::PAUSED, WAIT_TIMEOUT));

    // back to FOREGROUND and buffer underrun state
    EXPECT_CALL(*(m_mockMediaPlayer.get()), resume(_)).Times(1);
    m_audioPlayer->onFocusChanged(FocusState::FOREGROUND);
    ASSERT_TRUE(m_testAudioPlayerObserver->waitFor(PlayerActivity::PLAYING, WAIT_TIMEOUT));
    m_audioPlayer->onBufferUnderrun(m_mockMediaPlayer->getCurrentSourceId());
    ASSERT_TRUE(m_testAudioPlayerObserver->waitFor(PlayerActivity::BUFFER_UNDERRUN, WAIT_TIMEOUT));

    // expect stop when switching to NONE focus
    EXPECT_CALL(*(m_mockMediaPlayer.get()), stop(_)).Times(1);
    m_audioPlayer->onFocusChanged(FocusState::NONE);
    ASSERT_TRUE(m_testAudioPlayerObserver->waitFor(PlayerActivity::STOPPED, WAIT_TIMEOUT));
}

/**
 * Test an immediate focus change to background after play() has been called
 * Expect that pause() is called when @c AudioPlayer is pushed into background
 */

TEST_F(AudioPlayerTest, test_focusChangeToBackgroundBeforeOnPlaybackStarted) {
    EXPECT_CALL(*(m_mockMediaPlayer.get()), play(_)).Times(1);
    sendPlayDirective();

    // send clear queue directive and move to NONE focus
    EXPECT_CALL(*(m_mockMediaPlayer.get()), stop(_)).Times(AtLeast(1));

    sendClearQueueDirective();

    ASSERT_TRUE(m_testAudioPlayerObserver->waitFor(PlayerActivity::STOPPED, WAIT_TIMEOUT));
    m_audioPlayer->onFocusChanged(FocusState::NONE);

    // send a second Play directive and move to foreground
    EXPECT_CALL(*(m_mockFocusManager.get()), acquireChannel(CHANNEL_NAME, _, NAMESPACE_AUDIO_PLAYER))
        .Times(1)
        .WillOnce(Return(true));
    EXPECT_CALL(*(m_mockMediaPlayerTrack2.get()), play(_)).Times(1);
    auto avsMessageHeader = std::make_shared<AVSMessageHeader>(NAMESPACE_AUDIO_PLAYER, NAME_PLAY, MESSAGE_ID_TEST_2);

    std::shared_ptr<AVSDirective> playDirective = AVSDirective::create(
        "",
        avsMessageHeader,
        createEnqueuePayloadTest(OFFSET_IN_MILLISECONDS_TEST, AUDIO_ITEM_ID_2),
        m_attachmentManager,
        CONTEXT_ID_TEST_2);

    m_audioPlayer->CapabilityAgent::preHandleDirective(playDirective, std::move(m_mockDirectiveHandlerResult));
    m_audioPlayer->CapabilityAgent::handleDirective(MESSAGE_ID_TEST_2);

    m_audioPlayer->onFocusChanged(FocusState::FOREGROUND);
    ASSERT_TRUE(m_testAudioPlayerObserver->waitFor(PlayerActivity::PLAYING, WAIT_TIMEOUT));

    m_audioPlayer->onFocusChanged(FocusState::BACKGROUND);
    ASSERT_TRUE(m_testAudioPlayerObserver->waitFor(PlayerActivity::PAUSED, WAIT_TIMEOUT));
}

/**
 * Test @c onPlaybackError and expect AudioPlayer to change to STOPPED state and that it would go back to PLAYING state
 * when a new REPLACE_ALL Play directive comes in.
 */

TEST_F(AudioPlayerTest, test_playAfterOnPlaybackError) {
    EXPECT_CALL(*(m_mockMediaPlayer.get()), getOffset(_))
        .WillRepeatedly(Return(m_mockMediaPlayer->getOffset(m_mockMediaPlayer->getCurrentSourceId())));
    sendPlayDirective();
    ASSERT_TRUE(m_testAudioPlayerObserver->waitFor(PlayerActivity::PLAYING, WAIT_TIMEOUT));
    EXPECT_CALL(*(m_mockFocusManager.get()), releaseChannel(CHANNEL_NAME, _))
        .Times(1)
        .WillOnce(InvokeWithoutArgs(this, &AudioPlayerTest::wakeOnReleaseChannel));
    m_audioPlayer->onPlaybackError(
        m_mockMediaPlayer->getCurrentSourceId(), ErrorType::MEDIA_ERROR_UNKNOWN, "TEST_ERROR");
    ASSERT_TRUE(m_testAudioPlayerObserver->waitFor(PlayerActivity::STOPPED, WAIT_TIMEOUT));
    ASSERT_EQ(std::future_status::ready, m_wakeReleaseChannelFuture.wait_for(WAIT_TIMEOUT));
    m_audioPlayer->onFocusChanged(FocusState::NONE);

    // send a REPLACE_ALL Play directive to see if AudioPlayer can still play the new item
    EXPECT_CALL(*(m_mockMediaPlayerTrack2.get()), play(_)).Times(1);
    auto avsMessageHeader = std::make_shared<AVSMessageHeader>(NAMESPACE_AUDIO_PLAYER, NAME_PLAY, MESSAGE_ID_TEST_2);

    std::shared_ptr<AVSDirective> playDirective =
        AVSDirective::create("", avsMessageHeader, REPLACE_ALL_PAYLOAD_TEST, m_attachmentManager, CONTEXT_ID_TEST_2);

    m_wakeAcquireChannelPromise = std::promise<void>();
    m_wakeAcquireChannelFuture = m_wakeAcquireChannelPromise.get_future();
    EXPECT_CALL(*(m_mockFocusManager.get()), acquireChannel(CHANNEL_NAME, _, NAMESPACE_AUDIO_PLAYER))
        .Times(1)
        .WillOnce(InvokeWithoutArgs(this, &AudioPlayerTest::wakeOnAcquireChannel));
    m_audioPlayer->CapabilityAgent::preHandleDirective(playDirective, std::move(m_mockDirectiveHandlerResult));
    m_audioPlayer->CapabilityAgent::handleDirective(MESSAGE_ID_TEST_2);
    ASSERT_EQ(std::future_status::ready, m_wakeAcquireChannelFuture.wait_for(WAIT_TIMEOUT));
    m_audioPlayer->onFocusChanged(FocusState::FOREGROUND);
    ASSERT_TRUE(m_testAudioPlayerObserver->waitFor(PlayerActivity::PLAYING, WAIT_TIMEOUT));
}

/**
 * Test @c onPlaybackStarted calls the @c PlaybackRouter
 */
TEST_F(AudioPlayerTest, test_playbackStartedSwitchesHandler) {
    EXPECT_CALL(*m_mockPlaybackRouter, switchToDefaultHandler());
    sendPlayDirective();
}

/**
 * Test to verify that ProgressReportDelayElapsed Event is sent correctly.  This test is timing sensitive.
 */
TEST_F(AudioPlayerTest, test_progressReportDelayElapsed) {
    m_expectedMessages.insert({PROGRESS_REPORT_DELAY_ELAPSED_NAME, 0});

    EXPECT_CALL(*(m_mockMessageSender.get()), sendMessage(_))
        .Times(AtLeast(1))
        .WillRepeatedly(Invoke([this](std::shared_ptr<avsCommon::avs::MessageRequest> request) {
            std::lock_guard<std::mutex> lock(m_mutex);
            verifyMessageMap(request, &m_expectedMessages);
            m_messageSentTrigger.notify_one();
        }));

    sendPlayDirective(OFFSET_IN_MILLISECONDS_BEFORE_PROGRESS_REPORT_DELAY);

    std::this_thread::sleep_for(std::chrono::milliseconds(PROGRESS_REPORT_DELAY));

    std::unique_lock<std::mutex> lock(m_mutex);
    auto result = m_messageSentTrigger.wait_for(lock, WAIT_TIMEOUT, [this] {
        for (auto messageStatus : m_expectedMessages) {
            if (messageStatus.second != 1) {
                return false;
            }
        }
        return true;
    });
    ASSERT_TRUE(result);
}

/**
 * Test to verify that ProgressReportDelayElapsed Event is not sent when the delay is less than the offset.  This test
 * is timing sensitive.
 */
TEST_F(AudioPlayerTest, test_progressReportDelayElapsedDelayLessThanOffset) {
    m_expectedMessages.insert({PROGRESS_REPORT_DELAY_ELAPSED_NAME, 0});

    EXPECT_CALL(*(m_mockMessageSender.get()), sendMessage(_))
        .Times(AtLeast(1))
        .WillRepeatedly(Invoke([this](std::shared_ptr<avsCommon::avs::MessageRequest> request) {
            std::lock_guard<std::mutex> lock(m_mutex);
            verifyMessageMap(request, &m_expectedMessages);
            m_messageSentTrigger.notify_one();
        }));

    sendPlayDirective(OFFSET_IN_MILLISECONDS_AFTER_PROGRESS_REPORT_DELAY);

    std::this_thread::sleep_for(std::chrono::milliseconds(PROGRESS_REPORT_DELAY));

    std::unique_lock<std::mutex> lock(m_mutex);
    auto result = m_messageSentTrigger.wait_for(lock, WAIT_TIMEOUT, [this] {
        for (auto messageStatus : m_expectedMessages) {
            if (messageStatus.second != 0) {
                return false;
            }
        }
        return true;
    });
    ASSERT_TRUE(result);
}

/**
 * Test to verify that ProgressReportIntervalElapsed Event is sent when the interval is less than the offset.  There
 * will be a ProgressReportIntervalElapsed Event at 100, 200 and 300 ms.  This test is timing sensitive.
 */
TEST_F(AudioPlayerTest, testTimer_progressReportIntervalElapsed) {
    m_expectedMessages.insert({PROGRESS_REPORT_INTERVAL_ELAPSED_NAME, 0});

    EXPECT_CALL(*(m_mockMessageSender.get()), sendMessage(_))
        .Times(AtLeast(1))
        .WillRepeatedly(Invoke([this](std::shared_ptr<avsCommon::avs::MessageRequest> request) {
            std::lock_guard<std::mutex> lock(m_mutex);
            verifyMessageMap(request, &m_expectedMessages);
            m_messageSentTrigger.notify_one();
        }));

    sendPlayDirective(OFFSET_IN_MILLISECONDS_BEFORE_PROGRESS_REPORT_INTERVAL);

    std::this_thread::sleep_for(TIME_FOR_TWO_AND_A_HALF_INTERVAL_PERIODS);

    std::unique_lock<std::mutex> lock(m_mutex);
    auto result = m_messageSentTrigger.wait_for(lock, WAIT_TIMEOUT, [this] {
        for (auto messageStatus : m_expectedMessages) {
            if (messageStatus.second != 3) {
                return false;
            }
        }
        return true;
    });
    ASSERT_TRUE(result);
}

/**
 * Test to verify that ProgressReportIntervalElapsed Event is sent when the interval is less than the offset.  There
 * will be a ProgressReportIntervalElapsed Event at 200 and 300 ms.  This test is timing sensitive.
 */
TEST_F(AudioPlayerTest, test_progressReportIntervalElapsedIntervalLessThanOffset) {
    m_expectedMessages.insert({PROGRESS_REPORT_INTERVAL_ELAPSED_NAME, 0});

    EXPECT_CALL(*(m_mockMessageSender.get()), sendMessage(_))
        .Times(AtLeast(1))
        .WillRepeatedly(Invoke([this](std::shared_ptr<avsCommon::avs::MessageRequest> request) {
            std::lock_guard<std::mutex> lock(m_mutex);
            verifyMessageMap(request, &m_expectedMessages);
            m_messageSentTrigger.notify_one();
        }));

    sendPlayDirective(OFFSET_IN_MILLISECONDS_AFTER_PROGRESS_REPORT_INTERVAL);

    std::this_thread::sleep_for(TIME_FOR_TWO_AND_A_HALF_INTERVAL_PERIODS);

    std::unique_lock<std::mutex> lock(m_mutex);
    auto result = m_messageSentTrigger.wait_for(lock, WAIT_TIMEOUT, [this] {
        for (auto messageStatus : m_expectedMessages) {
            if (messageStatus.second != 2) {
                return false;
            }
        }
        return true;
    });
    ASSERT_TRUE(result);
}

/**
 * Test when @c AudioPlayer goes to BACKGROUND focus that it changes to PAUSED state.  And when another PLAY directive
 * with REPLACE_ALL behavior comes in, that it would go to STOPPED state, and will not start playing again until the
 * focus goes back to FOREGROUND.
 */

TEST_F(AudioPlayerTest, testSlow_playOnlyAfterForegroundFocus) {
    EXPECT_CALL(*(m_mockMediaPlayer.get()), getOffset(_))
        .WillRepeatedly(Return(m_mockMediaPlayer->getOffset(m_mockMediaPlayer->getCurrentSourceId())));
    sendPlayDirective();
    ASSERT_TRUE(m_testAudioPlayerObserver->waitFor(PlayerActivity::PLAYING, WAIT_TIMEOUT));
    m_audioPlayer->onPlaybackStarted(m_mockMediaPlayer->getCurrentSourceId());
    m_audioPlayer->onFocusChanged(FocusState::BACKGROUND);
    ASSERT_TRUE(m_testAudioPlayerObserver->waitFor(PlayerActivity::PAUSED, WAIT_TIMEOUT));

    // send a REPLACE_ALL Play directive
    auto avsMessageHeader = std::make_shared<AVSMessageHeader>(NAMESPACE_AUDIO_PLAYER, NAME_PLAY, MESSAGE_ID_TEST_2);

    std::shared_ptr<AVSDirective> playDirective =
        AVSDirective::create("", avsMessageHeader, REPLACE_ALL_PAYLOAD_TEST, m_attachmentManager, CONTEXT_ID_TEST_2);

    m_wakeAcquireChannelPromise = std::promise<void>();
    m_audioPlayer->CapabilityAgent::preHandleDirective(playDirective, std::move(m_mockDirectiveHandlerResult));
    EXPECT_CALL(*(m_mockMediaPlayerTrack2.get()), play(_)).Times(0);
    m_audioPlayer->CapabilityAgent::handleDirective(MESSAGE_ID_TEST_2);
    ASSERT_TRUE(m_testAudioPlayerObserver->waitFor(PlayerActivity::STOPPED, WAIT_TIMEOUT));

    // Now check play() is only called when focus to back to FOREGROUND
    EXPECT_CALL(*(m_mockMediaPlayerTrack2.get()), play(_)).Times(1);
    m_audioPlayer->onFocusChanged(FocusState::FOREGROUND);
    ASSERT_TRUE(m_testAudioPlayerObserver->waitFor(PlayerActivity::PLAYING, WAIT_TIMEOUT));
}

/**
 * Test when @c AudioPlayer starts to play but loses focus before the onPlaybackStarted callback is received.
 * After onPlaybackStarted is received, playback should stop.
 */
TEST_F(AudioPlayerTest, testTimer_playbackStartedCallbackAfterFocusLost) {
    EXPECT_CALL(*(m_mockMediaPlayer.get()), getOffset(_))
        .WillRepeatedly(Return(m_mockMediaPlayer->getOffset(m_mockMediaPlayer->getCurrentSourceId())));

    auto avsMessageHeader =
        std::make_shared<AVSMessageHeader>(NAMESPACE_AUDIO_PLAYER, NAME_PLAY, MESSAGE_ID_TEST, PLAY_REQUEST_ID_TEST);

    std::shared_ptr<AVSDirective> playDirective = AVSDirective::create(
        "",
        avsMessageHeader,
        createEnqueuePayloadTest(OFFSET_IN_MILLISECONDS_TEST),
        m_attachmentManager,
        CONTEXT_ID_TEST);
    EXPECT_CALL(*(m_mockFocusManager.get()), acquireChannel(CHANNEL_NAME, _, NAMESPACE_AUDIO_PLAYER))
        .Times(1)
        .WillOnce(InvokeWithoutArgs(this, &AudioPlayerTest::wakeOnAcquireChannel));

    EXPECT_CALL(*m_mockDirectiveHandlerResult, setCompleted());

    m_audioPlayer->CapabilityAgent::preHandleDirective(playDirective, std::move(m_mockDirectiveHandlerResult));
    m_audioPlayer->CapabilityAgent::handleDirective(MESSAGE_ID_TEST);

    {
        // Enforce the sequence.
        InSequence dummy;

        std::promise<void> playCalledPromise;
        std::future<void> playCalled = playCalledPromise.get_future();

        // Override play() behavior in MockMediaPlayer. We don't want play() to automatically call onPlaybackStarted.
        ON_CALL(*m_mockMediaPlayer, play(_)).WillByDefault(InvokeWithoutArgs([&playCalledPromise] {
            playCalledPromise.set_value();
            return true;
        }));
        EXPECT_CALL(*m_mockMediaPlayer, stop(_)).Times(1);

        // Wait for acquireFocus().
        ASSERT_EQ(std::future_status::ready, m_wakeAcquireChannelFuture.wait_for(WAIT_TIMEOUT));
        m_audioPlayer->onFocusChanged(FocusState::FOREGROUND);

        ASSERT_THAT(playCalled.wait_for(WAIT_TIMEOUT), Ne(std::future_status::timeout));

        m_audioPlayer->onFocusChanged(FocusState::NONE);
        m_audioPlayer->onPlaybackStarted(m_mockMediaPlayer->getSourceId());

        // Make sure stop() will be called, but STOPPED state is already true
        ASSERT_FALSE(m_testAudioPlayerObserver->waitFor(PlayerActivity::PLAYING, WAIT_TIMEOUT));
    }
}

/**
 * This test sets up a sequence of 1 REPLACE track, followed by 3 ENQUEUE tracks,
 * then tests that they are all played in sequence.
 * It is called from tests that set up different numbers of MediaPlayers in a Factory Pool
 * to ensure everything works smoothly.
 */
void AudioPlayerTest::testPlayEnqueFinishPlay() {
    // send a play directive
    sendPlayDirective();
    ASSERT_TRUE(m_testAudioPlayerObserver->waitFor(PlayerActivity::PLAYING, WAIT_TIMEOUT));

    std::this_thread::sleep_for(std::chrono::milliseconds(EVENT_PROCESS_DELAY));

    // Enqueue 3 tracks
    for (int i = 0; i < 3; i++) {
        std::string msgId = MESSAGE_ID_TEST + std::to_string(i);
        auto avsMessageHeader = std::make_shared<AVSMessageHeader>(NAMESPACE_AUDIO_PLAYER, NAME_PLAY, msgId);
        std::shared_ptr<AVSDirective> playDirective = AVSDirective::create(
            "",
            avsMessageHeader,
            createEnqueuePayloadTest(OFFSET_IN_MILLISECONDS_TEST, AUDIO_ITEM_ID_1 + std::to_string(i)),
            m_attachmentManager,
            CONTEXT_ID_TEST + std::to_string(i));
        m_audioPlayer->CapabilityAgent::preHandleDirective(playDirective, std::move(m_mockDirectiveHandlerResult));
        m_audioPlayer->CapabilityAgent::handleDirective(msgId);
    }

    m_audioPlayer->onPlaybackFinished(m_mockMediaPlayer->getCurrentSourceId());

    ASSERT_TRUE(m_testAudioPlayerObserver->waitFor(PlayerActivity::FINISHED, WAIT_TIMEOUT));
    ASSERT_TRUE(m_testAudioPlayerObserver->waitFor(PlayerActivity::PLAYING, WAIT_TIMEOUT));

    m_audioPlayer->onPlaybackFinished(m_mockMediaPlayer->getCurrentSourceId());

    ASSERT_TRUE(m_testAudioPlayerObserver->waitFor(PlayerActivity::FINISHED, WAIT_TIMEOUT));
    ASSERT_TRUE(m_testAudioPlayerObserver->waitFor(PlayerActivity::PLAYING, WAIT_TIMEOUT));

    m_audioPlayer->onPlaybackFinished(m_mockMediaPlayer->getCurrentSourceId());

    ASSERT_TRUE(m_testAudioPlayerObserver->waitFor(PlayerActivity::FINISHED, WAIT_TIMEOUT));
    ASSERT_TRUE(m_testAudioPlayerObserver->waitFor(PlayerActivity::PLAYING, WAIT_TIMEOUT));

    m_audioPlayer->onPlaybackFinished(m_mockMediaPlayer->getCurrentSourceId());
    ASSERT_TRUE(m_testAudioPlayerObserver->waitFor(PlayerActivity::FINISHED, WAIT_TIMEOUT));
}

TEST_F(AudioPlayerTest, test1PlayerPool_PlayEnqueFinishPlay) {
    reSetUp(1);

    testPlayEnqueFinishPlay();
}

TEST_F(AudioPlayerTest, test2PlayerPool_PlayEnqueFinishPlay) {
    reSetUp(2);

    testPlayEnqueFinishPlay();
}

TEST_F(AudioPlayerTest, test3PlayerPool_PlayEnqueFinishPlay) {
    reSetUp(3);

    testPlayEnqueFinishPlay();
}

void AudioPlayerTest::verifyMessageOrder(
    const std::vector<std::string>& orderedMessageList,
    int index,
    std::function<void()> trigger) {
    size_t nextIndex = index;
    EXPECT_CALL(*(m_mockMessageSender.get()), sendMessage(_))
        .Times(AtLeast(1))
        .WillRepeatedly(
            Invoke([this, orderedMessageList, &nextIndex](std::shared_ptr<avsCommon::avs::MessageRequest> request) {
                std::lock_guard<std::mutex> lock(m_mutex);
                if (nextIndex < orderedMessageList.size()) {
                    if (verifyMessage(request, orderedMessageList.at(nextIndex))) {
                        if (nextIndex < orderedMessageList.size()) {
                            nextIndex++;
                        }
                    }
                }
                m_messageSentTrigger.notify_one();
            }));

    trigger();

    {
        bool result;
        std::unique_lock<std::mutex> lock(m_mutex);
        result = m_messageSentTrigger.wait_for(lock, WAIT_TIMEOUT, [orderedMessageList, &nextIndex] {
            if (nextIndex == orderedMessageList.size()) {
                return true;
            }
            return false;
        });

        ASSERT_TRUE(result);
    }
}

TEST_F(AudioPlayerTest, test_playbackFinishedMessageOrder_1Player) {
    reSetUp(1);

    std::vector<std::string> expectedMessages;

    expectedMessages.push_back(PLAYBACK_STARTED_NAME);
    expectedMessages.push_back(PROGRESS_REPORT_DELAY_ELAPSED_NAME);

    verifyMessageOrder(expectedMessages, 0, [this] { sendPlayDirective(); });

    expectedMessages.push_back(PLAYBACK_NEARLY_FINISHED_NAME);
    expectedMessages.push_back(PLAYBACK_FINISHED_NAME);
    verifyMessageOrder(
        expectedMessages, 2, [this] { m_audioPlayer->onPlaybackFinished(m_mockMediaPlayer->getCurrentSourceId()); });
}

TEST_F(AudioPlayerTest, test_playbackFinishedMessageOrder_2Players) {
    reSetUp(2);

    std::vector<std::string> expectedMessages;

    expectedMessages.push_back(PLAYBACK_STARTED_NAME);
    expectedMessages.push_back(PLAYBACK_NEARLY_FINISHED_NAME);
    expectedMessages.push_back(PROGRESS_REPORT_DELAY_ELAPSED_NAME);

    verifyMessageOrder(expectedMessages, 0, [this] { sendPlayDirective(); });

    expectedMessages.push_back(PLAYBACK_FINISHED_NAME);
    verifyMessageOrder(
        expectedMessages, 3, [this] { m_audioPlayer->onPlaybackFinished(m_mockMediaPlayer->getCurrentSourceId()); });
}

TEST_F(AudioPlayerTest, test_playbackStoppedMessageOrder_1Player) {
    reSetUp(1);

    std::vector<std::string> expectedMessages;

    expectedMessages.push_back(PLAYBACK_STARTED_NAME);
    expectedMessages.push_back(PROGRESS_REPORT_DELAY_ELAPSED_NAME);

    verifyMessageOrder(expectedMessages, 0, [this] { sendPlayDirective(); });

    expectedMessages.push_back(PLAYBACK_STOPPED_NAME);
    verifyMessageOrder(
        expectedMessages, 2, [this] { m_audioPlayer->onPlaybackStopped(m_mockMediaPlayer->getCurrentSourceId()); });
}

TEST_F(AudioPlayerTest, test_playbackStoppedMessageOrder_2Players) {
    reSetUp(2);

    std::vector<std::string> expectedMessages;

    expectedMessages.push_back(PLAYBACK_STARTED_NAME);
    expectedMessages.push_back(PLAYBACK_NEARLY_FINISHED_NAME);
    expectedMessages.push_back(PROGRESS_REPORT_DELAY_ELAPSED_NAME);

    verifyMessageOrder(expectedMessages, 0, [this] { sendPlayDirective(); });

    expectedMessages.push_back(PLAYBACK_STOPPED_NAME);
    verifyMessageOrder(
        expectedMessages, 3, [this] { m_audioPlayer->onPlaybackStopped(m_mockMediaPlayer->getCurrentSourceId()); });
}
}  // namespace test
}  // namespace audioPlayer
}  // namespace capabilityAgents
}  // namespace alexaClientSDK
