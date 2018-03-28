#include <cstring>
#include "Event.h"
#include "Core.h"
#include "Thread.h"
#include "EventDescriptionBoard.h"
#include <containers.hpp>

namespace Brofiler
{
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
EventDescription* EventDescription::Create(intercept::types::r_string eventName, const char* fileName, const unsigned long fileLine, const unsigned long eventColor /*= Color::Null*/)
{
	static MT::Mutex creationLock;
	MT::ScopedGuard guard(creationLock);

	EventDescription* result = EventDescriptionBoard::Get().CreateDescription();
	result->name = eventName;
	result->file = fileName;
	result->line = fileLine;
	result->color = eventColor;
	return result;
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void EventDescription::DeleteAllDescriptions() {
	EventDescriptionBoard::Get().DeleteAllDescriptions();
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
EventDescription::EventDescription() : isSampling(false), hasUse(false), name(""), file(""), line(0), color(0)
{
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
EventDescription& EventDescription::operator=(const EventDescription&)
{
	BRO_FAILED("It is pointless to copy EventDescription. Please, check you logic!"); return *this; 
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
EventData* Event::Start(EventDescription& description)
{
	EventData* result = nullptr;

	if (EventStorage* storage = Core::storage)
	{
		result = &storage->NextEvent();
		result->description = &description;
        result->sourceCode = std::nullopt;
        result->thisArgs.clear();
		result->Start();

		if (description.isSampling)
		{
			storage->isSampling.IncFetch();
		}
	}
	return result;
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void Event::Stop(EventData& data)
{
	data.Stop();

	data.description->hasUse = true;
	if (data.description->isSampling)
	{
		if (EventStorage* storage = Core::storage)
		{
			storage->isSampling.DecFetch();
		}
	}
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void FiberSyncData::AttachToThread(EventStorage* storage, uint64_t threadId)
{
	if (storage)
	{
		FiberSyncData& data = storage->fiberSyncBuffer.Add();
		data.Start();
		data.finish = LLONG_MAX;
		data.threadId = threadId;
	}
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void FiberSyncData::DetachFromThread(EventStorage* storage)
{
	if (storage)
	{
		if (FiberSyncData* syncData = storage->fiberSyncBuffer.Back())
		{
			syncData->Stop();
		}
	}
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
OutputDataStream & operator<<(OutputDataStream &stream, const EventDescription &ob)
{
	if (!ob.hasUse) //Don't send full Description if it is irrelevant
		return stream << "" << "" << 0 << 0u << static_cast<byte>(0) << "";

	byte flags = (ob.isSampling ? 0x1 : 0);
	return stream << ob.name.c_str() << ob.file << ob.line << ob.color << flags << ob.source.c_str();
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
OutputDataStream& operator<<(OutputDataStream& stream, const EventTime& ob)
{
	return stream << ob.start << ob.finish;
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
OutputDataStream& operator<<(OutputDataStream& stream, const EventData& ob)
{
    stream << static_cast<EventTime>(ob) << ob.description->index;

	if (ob.sourceCode && !ob.sourceCode->empty())
		stream << static_cast<unsigned char>(1) << ob.sourceCode->c_str();
	else
		stream << static_cast<unsigned char>(0);

    stream << ob.thisArgs.c_str();

    return stream;
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
OutputDataStream& operator<<(OutputDataStream& stream, const SyncData& ob)
{
	return stream << static_cast<EventTime>(ob) << ob.core << ob.reason << ob.newThreadId;
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
OutputDataStream& operator<<(OutputDataStream& stream, const FiberSyncData& ob)
{
	return stream << static_cast<EventTime>(ob) << ob.threadId;
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
Category::Category(EventDescription& description) : Event(description)
{
	if (data)
	{
		if (EventStorage* storage = Core::storage)
		{
			storage->RegisterCategory(*data);
		}
	}
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
}
