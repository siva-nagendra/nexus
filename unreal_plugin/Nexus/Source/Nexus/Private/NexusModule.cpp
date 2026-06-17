// Copyright Nexus Team. All Rights Reserved.

#include "NexusModule.h"
#include "NexusTcpServer.h"

#define LOCTEXT_NAMESPACE "FNexusModule"

void FNexusModule::StartupModule()
{
    UE_LOG(LogNexus, Log, TEXT("Starting module..."));

    TcpServer = MakeShareable(new FNexusTcpServer(13377));
    if (TcpServer->Start())
    {
        UE_LOG(LogNexus, Log, TEXT("TCP server started on port 13377"));
    }
    else
    {
        UE_LOG(LogNexus, Error, TEXT("Failed to start TCP server"));
    }
}

void FNexusModule::ShutdownModule()
{
    if (TcpServer.IsValid())
    {
        TcpServer->Stop();
        TcpServer.Reset();
    }
    UE_LOG(LogNexus, Log, TEXT("Module shut down"));
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FNexusModule, Nexus)
