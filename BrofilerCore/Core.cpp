#include "Core.h"
#include "Common.h"
#include "Event.h"
#include "ProfilerServer.h"
#include "EventDescriptionBoard.h"
#include "Thread.h"

#include "Platform/SchedulerTrace.h"
#include "Platform/SamplingProfiler.h"
#include "Platform/SymbolEngine.h"


extern "C" Brofiler::EventData* NextEvent()
{
	if (Brofiler::EventStorage* storage = Brofiler::Core::storage)
	{
		return &storage->NextEvent();
	}

	return nullptr;
}


namespace Brofiler
{

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
ThreadDescription::ThreadDescription(const char* threadName, const MT::ThreadId& id, bool _fromOtherProcess, int32 _maxDepth /*= 1*/, int32 _priority /*= 0*/, uint32 _mask /*= 0*/)
	: threadID(id), fromOtherProcess(_fromOtherProcess), maxDepth(_maxDepth), priority(_priority), mask(_mask)
{
	#ifdef __linux__
	strcpy(name, threadName);
	#else
	strcpy_s(name, threadName);
	#endif
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
int64_t GetHighPrecisionTime()
{
	return MT::GetHighFrequencyTime();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void Core::DumpProgress(const char* message)
{
	progressReportedLastTimestampMS = MT::GetTimeMilliSeconds();

	OutputDataStream stream;
	stream << message;

	Server::Get().Send(DataResponse::ReportProgress, stream);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void Core::DumpEvents(const EventStorage& entry, const EventTime& timeSlice, ScopeData& scope)
{
	if (!entry.eventBuffer.IsEmpty())
	{
		const EventData* rootEvent = nullptr;

		entry.eventBuffer.ForEach([&](const EventData& data)
		{
			if (data.finish >= data.start && data.start >= timeSlice.start && timeSlice.finish >= data.finish)
			{
				if (!rootEvent)
				{
					rootEvent = &data;
					scope.InitRootEvent(*rootEvent);
				} 
				else if (rootEvent->finish < data.finish)
				{
					scope.Send();

					rootEvent = &data;
					scope.InitRootEvent(*rootEvent);
				}
				else
				{
					scope.AddEvent(data);
				}
			}
		});

		scope.Send();
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void Core::DumpThread(const ThreadEntry& entry, const EventTime& timeSlice, ScopeData& scope)
{
	// Events
	DumpEvents(entry.storage, timeSlice, scope);
	BRO_ASSERT(entry.storage.fiberSyncBuffer.IsEmpty(), "Fiber switch events in native threads?");
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void Core::DumpFiber(const FiberEntry& entry, const EventTime& timeSlice, ScopeData& scope)
{
	// Events
	DumpEvents(entry.storage, timeSlice, scope);

	if (!entry.storage.fiberSyncBuffer.IsEmpty())
	{
		OutputDataStream fiberSynchronizationStream;
		fiberSynchronizationStream << scope.header.boardNumber;
		fiberSynchronizationStream << scope.header.fiberNumber;
		fiberSynchronizationStream << entry.storage.fiberSyncBuffer;
		Server::Get().Send(DataResponse::FiberSynchronizationData, fiberSynchronizationStream);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void Core::DumpFrames(uint32 mode)
{
	if (frames.empty() || threads.empty())
		return;

	DumpProgress("Collecting Frame Events...");

	//Graphics::Image image;
	//graphics.GetScreenshot(image);

	EventTime timeSlice;
	timeSlice.start = frames.front().start;
	timeSlice.finish = frames.back().finish;

	uint32 boardNumber = DumpBoard(mode, timeSlice);

	ScopeData threadScope;
	threadScope.header.boardNumber = boardNumber;
	threadScope.header.fiberNumber = -1;

	for (size_t i = 0; i < threads.size(); ++i)
	{
		threadScope.header.threadNumber = (uint32)i;
		DumpThread(*threads[i], timeSlice, threadScope);
	}

	ScopeData fiberScope;
	fiberScope.header.boardNumber = (uint32)boardNumber;
	fiberScope.header.threadNumber = -1;
	for (size_t i = 0; i < fibers.size(); ++i)
	{
		fiberScope.header.fiberNumber = (uint32)i;
		DumpFiber(*fibers[i], timeSlice, fiberScope);
	}

	frames.clear();
	CleanupThreadsAndFibers();
	EventDescriptionBoard::Get().clearHasUseFlags();

	{
		DumpProgress("Serializing SwitchContexts");
		OutputDataStream switchContextsStream;
		switchContextsStream << boardNumber;
		switchContextCollector.Serialize(switchContextsStream);
		Server::Get().Send(DataResponse::SynchronizationData, switchContextsStream);
	}

	{
		DumpProgress("Serializing SysCalls");
		OutputDataStream callstacksStream;
		callstacksStream << boardNumber;
		syscallCollector.Serialize(callstacksStream);
		Server::Get().Send(DataResponse::SyscallPack, callstacksStream);
	}

	if (!callstackCollector.IsEmpty())
	{
		DumpProgress("Resolving callstacks");
		OutputDataStream symbolsStream;
		symbolsStream << boardNumber;
		callstackCollector.SerializeSymbols(symbolsStream);
		Server::Get().Send(DataResponse::CallstackDescriptionBoard, symbolsStream);

		DumpProgress("Serializing callstacks");
		OutputDataStream callstacksStream;
		callstacksStream << boardNumber;
		callstackCollector.SerializeCallstacks(callstacksStream);
		Server::Get().Send(DataResponse::CallstackPack, callstacksStream);
	}
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void Core::DumpSamplingData()
{
	if (samplingProfiler->StopSampling())
	{
		DumpProgress("Collecting Sampling Events...");

		OutputDataStream stream;
		samplingProfiler->Serialize(stream);

		DumpProgress("Sending Message With Sampling Data...");
		Server::Get().Send(DataResponse::SamplingFrame, stream);
	}
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void Core::CleanupThreadsAndFibers()
{
	MT::ScopedGuard guard(lock);

	for (ThreadList::iterator it = threads.begin(); it != threads.end();)
	{
		if (!(*it)->isAlive)
		{
			(*it)->~ThreadEntry();
			MT::Memory::Free(*it);
			it = threads.erase(it);
		}
		else
		{
			++it;
		}
	}

/*
	for (FiberList::iterator it = fibers.begin(); it != fibers.end(); ++it)
	{
		(*it)->~FiberEntry();
		MT::Memory::Free(*it);
	}
	fibers.clear();
*/
}

uint32 Core::DumpBoard(uint32 mode, EventTime timeSlice) {

	OutputDataStream stringStream;
	std::map<uint64_t, intercept::types::r_string> stringMap;
	//uint64_t stringSize;

	for (auto i = 0u; i < threads.size(); ++i)
		threads[i]->storage.eventBuffer.ForEach([&](const EventData& data) {
			if (data.sourceCode) {
				stringMap.insert({ reinterpret_cast<uint64_t>(data.sourceCode->data()), *data.sourceCode });
				//stringSize += data.sourceCode->size();
			}
				
			if (data.altName) {
				stringMap.insert({ reinterpret_cast<uint64_t>(data.altName->data()), *data.altName });
				//stringSize += data.altName->size();
			}
				
		});

	for (auto& it : EventDescriptionBoard::Get().GetEvents()) {
		stringMap.insert({ reinterpret_cast<uint64_t>(it->source.data()), it->source });
		//stringSize += it->source.size();
	}
	//OutputDebugStringA(std::to_string(stringSize).c_str());
	stringStream << static_cast<uint32_t>(stringMap.size());
	//stringSize = 0;
	for (auto& it : stringMap) {
		stringStream << it.first << it.second.c_str();
		//stringSize += it.second.size();
	}
	//OutputDebugStringA(std::to_string(stringSize).c_str());
	Server::Get().Send(DataResponse::TagsPack, stringStream);



	uint32 mainThreadIndex = 0;

	for (size_t i = 0; i < threads.size(); ++i)
		if (threads[i]->description.threadID.IsEqual(mainThreadID))
			mainThreadIndex = (uint32)i;

	OutputDataStream boardStream;

	static uint32 boardNumber = 0;
	boardStream << ++boardNumber;
	boardStream << MT::GetFrequency();
	boardStream << (uint64)0; // Origin
	boardStream << (uint32)0; // Precision
	boardStream << timeSlice;
	boardStream << threads;
	boardStream << mainThreadIndex;
	boardStream << EventDescriptionBoard::Get();
	Server::Get().Send(DataResponse::FrameDescriptionBoard, boardStream);

	return boardNumber;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
Core::Core() : progressReportedLastTimestampMS(0), isActive(false)
{
	schedulerTrace = SchedulerTrace::Get();
	samplingProfiler = SamplingProfiler::Get();
	symbolEngine = SymbolEngine::Get();
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void Core::Update()
{
	MT::ScopedGuard guard(lock);
	
	if (isActive)
	{
		if (!frames.empty())
			frames.back().Stop();

		if (IsTimeToReportProgress())
			DumpCapturingProgress();		
	}

	UpdateEvents();

	if (isActive)
	{
		frames.push_back(EventTime());
		frames.back().Start();
	}
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void Core::UpdateEvents()
{
	if (!mainThreadID.IsValid())
	{
		mainThreadID = MT::ThreadId::Self();
	}

	Server::Get().Update();
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void Core::ReportSysCall(const SysCallDesc& desc)
{
	syscallCollector.Add(desc);
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
bool Core::ReportSwitchContext(const SwitchContextDesc& desc)
{
	if (!schedulerTrace)
	{
		return false;
	}

	// finalize work interval
	// auto oldThreadIt = schedulerTrace->activeThreadsIDs.find(desc.oldThreadId);
	// if (oldThreadIt != schedulerTrace->activeThreadsIDs.end())
	{
		switchContextCollector.Add(desc);
	}

	return true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
bool Core::ReportStackWalk(const CallstackDesc& desc)
{
	if (!schedulerTrace)
	{
		return false;
	}

	auto it = schedulerTrace->activeThreadsIDs.find(desc.threadID);
	if (it == schedulerTrace->activeThreadsIDs.end())
	{
		return false;
	}

	callstackCollector.Add(desc);
	return true;
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void Core::StartSampling()
{
	samplingProfiler->StartSampling(threads);
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void Core::Activate( bool active )
{
	if (isActive != active)
	{
		isActive = active;

		for(auto it = threads.begin(); it != threads.end(); ++it)
		{
			ThreadEntry* entry = *it;
			entry->Activate(active);
		}

/*
		for(auto it = fibers.begin(); it != fibers.end(); ++it)
		{
			FiberEntry* entry = *it;
			entry->Activate(active);
		}
*/

		if (active && false)
		{
			CaptureStatus::Type status = schedulerTrace->Start(SchedulerTrace::ALL, threads, true);

			// Let's retry with more narrow setup
			if (status != CaptureStatus::OK)
				status = schedulerTrace->Start(SchedulerTrace::SWITCH_CONTEXTS, threads, true);

			SendHandshakeResponse(status);
		}
		else
		{
			schedulerTrace->Stop();
		}
	}
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void Core::DumpCapturingProgress()
{
	std::stringstream stream;

	if (isActive)
		stream << "Capturing Frame " << (uint32)frames.size() << std::endl;

	if (samplingProfiler->IsActive())
		stream << "Sample Count " << (uint32)samplingProfiler->GetCollectedCount() << std::endl;

	DumpProgress(stream.str().c_str());
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
bool Core::IsTimeToReportProgress() const
{
	return MT::GetTimeMilliSeconds() > progressReportedLastTimestampMS + 200;
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void Core::SendHandshakeResponse(CaptureStatus::Type status)
{
	OutputDataStream stream;
	stream << (uint32)status;
	Server::Get().Send(DataResponse::Handshake, stream);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
bool Core::IsRegistredThread(MT::ThreadId id)
{
	MT::ScopedGuard guard(lock);
	for (ThreadList::iterator it = threads.begin(); it != threads.end(); ++it)
	{
		ThreadEntry* entry = *it;
		if (entry->description.threadID.IsEqual(id))
		{
			return true;
		}
	}
	return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
bool Core::RegisterThread(const ThreadDescription& description, EventStorage** slot)
{
	MT::ScopedGuard guard(lock);
	ThreadEntry* entry = new (MT::Memory::Alloc(sizeof(ThreadEntry), BRO_CACHE_LINE_SIZE)) ThreadEntry(description, slot);
	threads.push_back(entry);

	if (slot != nullptr)
		*slot = &entry->storage;

	return true;
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
bool Core::UnRegisterThread(MT::ThreadId threadID)
{
	MT::ScopedGuard guard(lock);
	for (ThreadList::iterator it = threads.begin(); it != threads.end(); ++it)
	{
		ThreadEntry* entry = *it;
		if (entry->description.threadID.IsEqual(threadID) && entry->isAlive)
		{
			if (!isActive)
			{
				entry->~ThreadEntry();
				MT::Memory::Free(entry);
				threads.erase(it);
				return true;
			}
			else
			{
				entry->isAlive = false;
				return true;
			}
		}
	}

	return false;
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
bool Core::RegisterFiber(const FiberDescription& description, EventStorage** slot)
{
	MT::ScopedGuard guard(lock);
	FiberEntry* entry = new (MT::Memory::Alloc(sizeof(FiberEntry), BRO_CACHE_LINE_SIZE)) FiberEntry(description);
	fibers.push_back(entry);
	entry->storage.isFiberStorage = true;
	*slot = &entry->storage;
	return true;
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
Core::~Core()
{
	MT::ScopedGuard guard(lock);

	for (ThreadList::iterator it = threads.begin(); it != threads.end(); ++it)
	{
		(*it)->~ThreadEntry();
		MT::Memory::Free((*it));
	}
	threads.clear();

	for (FiberList::iterator it = fibers.begin(); it != fibers.end(); ++it)
	{
		(*it)->~FiberEntry();
		MT::Memory::Free((*it));
	}
	fibers.clear();
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
const std::vector<ThreadEntry*>& Core::GetThreads() const
{
	return threads;
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
mt_thread_local EventStorage* Core::storage = nullptr;
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
Core Core::notThreadSafeInstance;
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
ScopeHeader::ScopeHeader() : boardNumber(0), threadNumber(0)
{

}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
OutputDataStream& operator<<(OutputDataStream& stream, const ScopeHeader& header)
{
	return stream << header.boardNumber << header.threadNumber << header.fiberNumber << header.event;
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
OutputDataStream& operator<<(OutputDataStream& stream, const ScopeData& ob)
{
	return stream << ob.header << ob.categories << ob.events;
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
OutputDataStream& operator<<(OutputDataStream& stream, const ThreadDescription& description)
{
	return stream << description.threadID.AsUInt64() << description.name << description.maxDepth << description.priority << description.mask;
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
OutputDataStream& operator<<(OutputDataStream& stream, const ThreadEntry* entry)
{
	return stream << entry->description;
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
OutputDataStream& operator<<(OutputDataStream& stream, const FiberDescription& description)
{
	return stream << description.id;
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
OutputDataStream& operator<<(OutputDataStream& stream, const FiberEntry* entry)
{
	return stream << entry->description;
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
BROFILER_API void NextFrame()
{
	return Core::NextFrame();
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
BROFILER_API bool IsActive()
{
	return Core::Get().isActive;
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
BROFILER_API EventStorage** GetEventStorageSlotForCurrentThread()
{
	return &Core::Get().storage;
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
BROFILER_API bool IsFiberStorage(EventStorage* fiberStorage)
{
	return fiberStorage->isFiberStorage;
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
BROFILER_API bool RegisterThread(const char* name)
{
	return Core::Get().RegisterThread(ThreadDescription(name, MT::ThreadId::Self(), false), &Core::storage);
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
BROFILER_API bool RegisterThread(const wchar_t* name)
{
	char mbName[ThreadDescription::THREAD_NAME_LENGTH];
	wcstombs(mbName, name, ThreadDescription::THREAD_NAME_LENGTH);
	return Core::Get().RegisterThread(ThreadDescription(mbName, MT::ThreadId::Self(), false), &Core::storage);
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
BROFILER_API bool UnRegisterThread()
{
	return Core::Get().UnRegisterThread(MT::ThreadId::Self());
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
BROFILER_API bool RegisterFiber(uint64 fiberId, EventStorage** slot)
{
	return Core::Get().RegisterFiber(FiberDescription(fiberId), slot);
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
EventStorage::EventStorage(): isSampling(0), isFiberStorage(false)
{
	 
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void ThreadEntry::Activate(bool isActive)
{
	if (!isAlive)
		return;

	if (isActive)
		storage.Clear(true);

	if (threadTLS != nullptr)
	{
		*threadTLS = isActive ? &storage : nullptr;
	}
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
bool IsSleepOnlyScope(const ScopeData& scope)
{
	if (!scope.categories.empty())
		return false;

	const std::vector<EventData>& events = scope.events;
	for(auto it = events.begin(); it != events.end(); ++it)
	{
		const EventData& data = *it;

		if (data.description->color != Color::White)
		{
			return false;
		}
	}

	return true;
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void ScopeData::Send()
{
	if (!events.empty() || !categories.empty())
	{
		if (!IsSleepOnlyScope(*this))
		{
			OutputDataStream frameStream;
			frameStream << *this;
			Server::Get().Send(DataResponse::EventFrame, frameStream);
		}
	}

	Clear();
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void ScopeData::Clear()
{
	events.clear();
	categories.clear();
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
}
