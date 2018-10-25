#include <cstring>
#include <mutex>
#include <thread>

#include "Event.h"
#include "Core.h"
#include "EventDescriptionBoard.h"
#include <containers.hpp>

namespace Brofiler
{
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
EventDescription* EventDescription::Create(intercept::types::r_string eventName, const char* fileName, const unsigned long fileLine, const unsigned long eventColor /*= Color::Null*/, const unsigned long filter /*= 0*/)
{
	return EventDescriptionBoard::Get().CreateDescription(eventName, fileName, fileLine, eventColor, filter);
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
EventDescription* EventDescription::CreateShared(intercept::types::r_string eventName, const char* fileName, const unsigned long fileLine, const unsigned long eventColor /*= Color::Null*/, const unsigned long filter /*= 0*/)
{
	return EventDescriptionBoard::Get().CreateSharedDescription(eventName, fileName, fileLine, eventColor, filter);
}
void EventDescription::DeleteAllDescriptions() {
    EventDescriptionBoard::Get().DeleteAllDescriptions();
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
EventDescription::EventDescription() : name(""), file(""), line(0), color(0)
{
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
EventDescription& EventDescription::operator=(const EventDescription&)
{
	BRO_FAILED("It is pointless to copy EventDescription. Please, check you logic!"); return *this; 
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
EventData* Event::Start(const EventDescription& description)
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
	}
	return result;
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void Event::Stop(EventData& data)
{
	data.Stop();
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void BRO_INLINE PushEvent(EventStorage* pStorage, const EventDescription* description, int64_t timestampStart)
{
	if (EventStorage* storage = pStorage)
	{
		EventData& result = storage->NextEvent();
		result.description = description;
		result.start = timestampStart;
		storage->pushPopEventStack[storage->pushPopEventStackIndex++] = &result;
	}
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void BRO_INLINE PopEvent(EventStorage* pStorage, int64_t timestampFinish)
{
	if (EventStorage* storage = pStorage)
		if (storage->pushPopEventStackIndex > 0)
			storage->pushPopEventStack[--storage->pushPopEventStackIndex]->finish = timestampFinish;
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void Event::Push(intercept::types::r_string name)
{
	if (EventStorage* storage = Core::storage)
	{
		EventDescription* desc = EventDescription::CreateShared(name);
		Push(*desc);
	}
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void Event::Push(const EventDescription& description)
{
	PushEvent(Core::storage, &description, Brofiler::GetHighPrecisionTime());
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void Event::Pop()
{
	PopEvent(Core::storage, Brofiler::GetHighPrecisionTime());
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void Event::Add(EventStorage* storage, const EventDescription* description, int64_t timestampStart, int64_t timestampFinish)
{
	EventData& data = storage->eventBuffer.Add();
	data.description = description;
	data.start = timestampStart;
	data.finish = timestampFinish;
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void Event::Push(EventStorage* storage, const EventDescription* description, int64_t timestampStart)
{
	PushEvent(storage, description, timestampStart);
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void Event::Pop(EventStorage* storage, int64_t timestampFinish)
{
	PopEvent(storage, timestampFinish);
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
void Tag::Attach(const EventDescription& description, float val)
{
	if (EventStorage* storage = Core::storage)
	{
		storage->tagFloatBuffer.Add(TagFloat(description, val));
	}
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void Tag::Attach(const EventDescription& description, int32_t val)
{
	if (EventStorage* storage = Core::storage)
	{
		storage->tagS32Buffer.Add(TagS32(description, val));
	}
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void Tag::Attach(const EventDescription& description, uint32_t val)
{
	if (EventStorage* storage = Core::storage)
	{
		storage->tagU32Buffer.Add(TagU32(description, val));
	}
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void Tag::Attach(const EventDescription& description, uint64_t val)
{
	if (EventStorage* storage = Core::storage)
	{
		storage->tagU64Buffer.Add(TagU64(description, val));
	}
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void Tag::Attach(const EventDescription& description, float val[3])
{
	if (EventStorage* storage = Core::storage)
	{
		storage->tagPointBuffer.Add(TagPoint(description, val));
	}
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void Tag::Attach(const EventDescription& description, const char* val)
{
	if (EventStorage* storage = Core::storage)
	{
		storage->tagStringBuffer.Add(TagString(description, val));
	}
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
OutputDataStream & operator<<(OutputDataStream &stream, const EventDescription &ob)
{
	byte flags = 0;
	return stream << ob.name.c_str() << ob.file << ob.line << ob.filter << ob.color << (float)0.0f << flags << reinterpret_cast<uint64_t>(ob.source.hash());
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
OutputDataStream& operator<<(OutputDataStream& stream, const EventTime& ob)
{
	return stream << ob.start << ob.finish;
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
OutputDataStream& operator<<(OutputDataStream& stream, const EventData& ob)
{
    stream << static_cast<EventTime>(ob) << (ob.description ? ob.description->index : (uint32)-1);

    if (ob.sourceCode && !ob.sourceCode->empty())
        stream << static_cast<unsigned char>(1) << reinterpret_cast<uint64_t>(ob.sourceCode->hash());
    else if (ob.altName && !ob.altName->empty())
        stream << static_cast<unsigned char>(2) << reinterpret_cast<uint64_t>(ob.altName->hash());
    else if (ob.sourceCode && !ob.sourceCode->empty() && ob.altName && !ob.altName->empty())
        stream << static_cast<unsigned char>(3) << reinterpret_cast<uint64_t>(ob.altName->c_str()) << reinterpret_cast<uint64_t>(ob.sourceCode->hash());
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
Category::Category(const EventDescription& description) : Event(description)
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
