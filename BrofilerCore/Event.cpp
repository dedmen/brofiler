#include <cstring>
#include "Event.h"
#include "Core.h"
#include "Thread.h"
#include "EventDescriptionBoard.h"
#include <containers.hpp>

namespace Brofiler
{
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
EventDescription* EventDescription::Create(intercept::types::r_string eventName, const char* fileName, const unsigned long fileLine, const unsigned long eventColor /*= Color::Null*/, const unsigned long filter /*= 0*/, float budget /*= 0.0f*/)
{
	static MT::Mutex creationLock;
	MT::ScopedGuard guard(creationLock);

	EventDescription* result = EventDescriptionBoard::Get().CreateDescription();
	result->name = eventName;
	result->file = fileName;
	result->line = fileLine;
	result->color = eventColor;
	result->budget = budget;
	result->filter = filter;
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
		result->altName = std::nullopt;
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
		return stream << "" << "" << 0 << 0 << 0u << 0.f << static_cast<byte>(0) << reinterpret_cast<uint64_t>(intercept::types::r_string().data());

	byte flags = (ob.isSampling ? 0x1 : 0);
	return stream << ob.name.c_str() << ob.file << ob.line << ob.filter << ob.color << ob.budget << flags << reinterpret_cast<uint64_t>(ob.source.data());
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
	
	if (ob.sourceCode && !ob.sourceCode->empty() && ob.altName && !ob.altName->empty())
		stream << static_cast<unsigned char>(3) << reinterpret_cast<uint64_t>(ob.altName->data()) << reinterpret_cast<uint64_t>(ob.sourceCode->data());
	else if (ob.sourceCode && !ob.sourceCode->empty())
		stream << static_cast<unsigned char>(1) << reinterpret_cast<uint64_t>(ob.sourceCode->data());
	else if (ob.altName && !ob.altName->empty())
		stream << static_cast<unsigned char>(2) << reinterpret_cast<uint64_t>(ob.altName->data());
	else														
		stream << static_cast<unsigned char>(0);

    stream << static_cast<intercept::types::r_string>(ob.thisArgs).c_str();

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
