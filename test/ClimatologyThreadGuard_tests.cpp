#include <gtest/gtest.h>

#include "ClimatologyThreadGuard.h"

class ClimatologyThreadGuardTests : public ::testing::Test {
protected:
  void SetUp() override { ClimatologyThreadGuard::Reset(); }
};

TEST_F(ClimatologyThreadGuardTests, MainThreadMayInitializeAnyService) {
  EXPECT_TRUE(ClimatologyThreadGuard::CanInvoke(ClimatologyService::Wind,
                                                true));
  EXPECT_TRUE(ClimatologyThreadGuard::CanInvoke(ClimatologyService::Current,
                                                true));
}

TEST_F(ClimatologyThreadGuardTests, WorkerIsBlockedUntilServicePrepared) {
  EXPECT_FALSE(ClimatologyThreadGuard::CanInvoke(ClimatologyService::Current,
                                                 false));
  ClimatologyThreadGuard::MarkPrepared(ClimatologyService::Current);
  EXPECT_TRUE(ClimatologyThreadGuard::CanInvoke(ClimatologyService::Current,
                                                false));
}

TEST_F(ClimatologyThreadGuardTests, PreparationIsIsolatedPerService) {
  ClimatologyThreadGuard::MarkPrepared(ClimatologyService::Wind);
  EXPECT_TRUE(ClimatologyThreadGuard::CanInvoke(ClimatologyService::Wind,
                                                false));
  EXPECT_FALSE(ClimatologyThreadGuard::CanInvoke(ClimatologyService::Current,
                                                 false));
  EXPECT_FALSE(ClimatologyThreadGuard::CanInvoke(
      ClimatologyService::WindAtlas, false));
  EXPECT_FALSE(ClimatologyThreadGuard::CanInvoke(
      ClimatologyService::CycloneTracks, false));
}

TEST_F(ClimatologyThreadGuardTests, BlockDiagnosticIsLoggedOncePerService) {
  EXPECT_TRUE(
      ClimatologyThreadGuard::ShouldLogBlocked(ClimatologyService::Current));
  EXPECT_FALSE(
      ClimatologyThreadGuard::ShouldLogBlocked(ClimatologyService::Current));
  EXPECT_TRUE(
      ClimatologyThreadGuard::ShouldLogBlocked(ClimatologyService::Wind));
}

TEST_F(ClimatologyThreadGuardTests, ResetRevokesWorkerAccess) {
  ClimatologyThreadGuard::MarkPrepared(ClimatologyService::Current);
  ASSERT_TRUE(ClimatologyThreadGuard::CanInvoke(ClimatologyService::Current,
                                                false));
  ClimatologyThreadGuard::Reset();
  EXPECT_FALSE(ClimatologyThreadGuard::CanInvoke(ClimatologyService::Current,
                                                 false));
}
