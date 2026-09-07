#include "support/FaultInjectingGameTransport.h"

#include <gtest/gtest.h>

TEST(FaultInjectingGameTransportTests, DelaysCommandsAndPreservesTheirOrder)
{
    FaultInjectionProfile profile;
    profile.clientToHostDelayMs = 50;
    FaultInjectingGameTransport transport(profile);

    transport.SendClientCommand("first");
    transport.SendClientCommand("second");

    transport.AdvanceTime(49);
    EXPECT_TRUE(transport.ReceiveHostCommands().empty());

    transport.AdvanceTime(1);
    EXPECT_EQ(transport.ReceiveHostCommands(), (std::vector<std::string>{"first", "second"}));
    EXPECT_EQ(transport.GetPingMs(), 50);
}

TEST(FaultInjectingGameTransportTests, StallDefersHostTrafficWithoutDroppingIt)
{
    FaultInjectionProfile profile;
    profile.hostToClientDelayMs = 20;
    FaultInjectingGameTransport transport(profile);

    transport.SetDirectionStalled(TransportDirection::HostToClient, true);
    transport.SendHostFrame("frame-1");
    transport.SendHostSnapshot("snapshot-1");
    transport.AdvanceTime(100);

    EXPECT_TRUE(transport.ReceiveClientFrames().empty());
    EXPECT_TRUE(transport.ReceiveClientSnapshots().empty());
    EXPECT_EQ(transport.GetPendingMessageCount(), 2u);

    transport.SetDirectionStalled(TransportDirection::HostToClient, false);
    EXPECT_EQ(transport.ReceiveClientFrames(), (std::vector<std::string>{"frame-1"}));
    EXPECT_EQ(transport.ReceiveClientSnapshots(), (std::vector<std::string>{"snapshot-1"}));
    EXPECT_EQ(transport.GetPendingMessageCount(), 0u);
}

TEST(FaultInjectingGameTransportTests, JitterStaysWithinConfiguredDeliveryWindow)
{
    FaultInjectionProfile profile;
    profile.clientToHostDelayMs = 50;
    profile.clientToHostJitterMs = 10;
    FaultInjectingGameTransport transport(profile);

    transport.SendClientCommand("jittered");

    // Base delay minus configured jitter is the earliest legal delivery time.
    transport.AdvanceTime(39);
    EXPECT_TRUE(transport.ReceiveHostCommands().empty());

    // Base delay plus configured jitter is the latest legal delivery time.
    transport.AdvanceTime(21);
    EXPECT_EQ(transport.ReceiveHostCommands(), (std::vector<std::string>{"jittered"}));
}

TEST(FaultInjectingGameTransportTests, DisconnectDropsInflightTrafficAndAllowsNewConnection)
{
    FaultInjectionProfile profile;
    profile.clientToHostDelayMs = 10;
    FaultInjectingGameTransport transport(profile);

    transport.SendClientCommand("lost-on-disconnect");
    transport.SetConnected(false);

    EXPECT_FALSE(transport.IsConnected());
    EXPECT_EQ(transport.GetStatus(), "Fault transport disconnected");
    transport.AdvanceTime(100);
    EXPECT_TRUE(transport.ReceiveHostCommands().empty());

    transport.SetConnected(true);
    transport.SendClientCommand("after-reconnect");
    transport.AdvanceTime(10);
    EXPECT_EQ(transport.ReceiveHostCommands(), (std::vector<std::string>{"after-reconnect"}));
}

TEST(FaultInjectingGameTransportTests, CanDeterministicallyDuplicateApplicationMessages)
{
    FaultInjectionProfile profile;
    profile.duplicateEveryNthMessage = 2;
    FaultInjectingGameTransport transport(profile);

    transport.SendClientCommand("one");
    transport.SendClientCommand("two");
    transport.SendClientCommand("three");

    EXPECT_EQ(transport.ReceiveHostCommands(),
              (std::vector<std::string>{"one", "two", "two", "three"}));
}
