#include "EventDescriptionBoard.h"
#include "Event.h"

#include <mutex>
#include <containers.hpp>

namespace Brofiler
{
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
static std::mutex& GetBoardLock()
{
	// Initialize as static local variable to prevent problems with static initialization order
	static std::mutex lock;
	return lock;
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
EventDescriptionBoard& EventDescriptionBoard::Get()
{ 
	return instance;
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
const EventDescriptionList& EventDescriptionBoard::GetEvents() const
{
	return boardDescriptions;
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
EventDescription* EventDescriptionBoard::CreateDescription(intercept::types::r_string name, intercept::types::r_string file /*= nullptr*/, uint32_t line /*= 0*/, uint32_t color /*= Color::Null*/, uint32_t filter /*= 0*/)
{
	std::lock_guard<std::mutex> lock(GetBoardLock());

	size_t index = boardDescriptions.Size();

	EventDescription& desc = boardDescriptions.Add();
	desc.index = (uint32)index;
	desc.name = name;
	desc.file = file;
	desc.line = line;
	desc.color = color;
	desc.filter = filter;

	return &desc;
}

void EventDescriptionBoard::DeleteAllDescriptions() {
    std::lock_guard<std::mutex> lock(GetBoardLock());
    __debugbreak();
}
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
EventDescription* EventDescriptionBoard::CreateSharedDescription(intercept::types::r_string name, intercept::types::r_string file /*= nullptr*/, uint32_t line /*= 0*/, uint32_t color /*= Color::Null*/, uint32_t filter /*= 0*/)
{
	StringHash nameHash(name.c_str());

	std::lock_guard<std::mutex> lock(sharedLock);

	std::pair<DescriptionMap::iterator, bool> cached = sharedDescriptions.insert({ nameHash, nullptr });

	if (cached.second)
	{
		cached.first->second = CreateDescription(name, file, line, color, filter);
	}

	return cached.first->second;
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
OutputDataStream& operator << ( OutputDataStream& stream, const EventDescriptionBoard& ob)
{
	std::lock_guard<std::mutex> lock(GetBoardLock());
	stream << ob.GetEvents();
	return stream;
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
Brofiler::EventDescriptionBoard EventDescriptionBoard::instance;
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
}
