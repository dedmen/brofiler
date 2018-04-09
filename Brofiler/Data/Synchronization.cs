﻿using System;
using System.Collections.Generic;
using System.IO;

namespace Profiler.Data
{
    public enum SyncReason
    {
        // Windows and XBox
        Win_Executive = 0,
        Win_FreePage,
        Win_PageIn,
        Win_PoolAllocation,
        Win_DelayExecution,
        Win_Suspended,
        Win_UserRequest,
        Win_WrExecutive,
        Win_WrFreePage,
        Win_WrPageIn,
        Win_WrPoolAllocation,
        Win_WrDelayExecution,
        Win_WrSuspended,
        Win_WrUserRequest,
        Win_WrEventPair,
        Win_WrQueue,
        Win_WrLpcReceive,
        Win_WrLpcReply,
        Win_WrVirtualMemory,
        Win_WrPageOut,
        Win_WrRendezvous,
        Win_WrKeyedEvent,
        Win_WrTerminated,
        Win_WrProcessInSwap,
        Win_WrCpuRateControl,
        Win_WrCalloutStack,
        Win_WrKernel,
        Win_WrResource,
        Win_WrPushLock,
        Win_WrMutex,
        Win_WrQuantumEnd,
        Win_WrDispatchInt,
        Win_WrPreempted,
        Win_WrYieldExecution,
        Win_WrFastMutex,
        Win_WrGuardedMutex,
        Win_WrRundown,
        Win_MaximumWaitReason,

        SyncReasonCount,

		SyncReasonActive
    }


	public enum CallStackReason
	{

		MaxReasonsCount,

		SysCall = (Int32.MaxValue - 1),
		AutoSample = Int32.MaxValue
	}

    public struct SyncEvent : IComparable<SyncEvent>
    {
        public Tick Timestamp;
        public UInt64 OldThreadID;
        public UInt64 NewThreadID;
        public byte CPUID;
        public SyncReason Reason;
        public SyncEvent(BinaryReader reader)
        {
            Timestamp = new Tick() { Start = Durable.ReadTime(reader) };
            OldThreadID = reader.ReadUInt64();
            NewThreadID = reader.ReadUInt64();
            CPUID = reader.ReadByte();
            Reason = (SyncReason)reader.ReadByte();
        }
        public int CompareTo(SyncEvent other)
        {
            return Timestamp.Start.CompareTo(other.Timestamp.Start);
        }
    }

    public class SyncInterval : Durable
    {
        public SyncReason Reason { get; set; }
		public UInt64 NewThreadId { get; set; }
        public byte Core { get; set; }
    }

    public class ThisArgs : Durable
    {
        public SyncReason Reason { get; set; }
		public byte core;

		public ThreadDescription newThreadDesc;
		public string args;

		public string ReasonText
		{
			get
			{
				return args;
			}
		}

        public ThisArgs() { }
    }

    public class NodeWaitInterval : Durable
    {
        public int Count { get; set; }
        public Durable NodeInterval { get; set; }
        public SyncReason Reason { get; set; }
        public double Percent
        {
            get
            {
                long nodeTime = (NodeInterval.Finish - NodeInterval.Start);
                long waitTime = (this.Finish - this.Start);
                double percent = ((double)waitTime / (double)nodeTime) * 100.0;
                return percent;
            }
        }

        public string Desc
        {
            get
            {
                return Percent.ToString("F3") + "%, " + DurationF3 + " ms, " + Count;
            }
        }


        public NodeWaitInterval() { }
    }

    public class NodeWaitIntervalList : List<NodeWaitInterval> {}

    public class SynchronizationMap : IResponseHolder
    {
        public override DataResponse Response { get; set; }
        public FrameGroup Group { get; set; }
        public Dictionary<UInt64, Synchronization> SyncMap { get; set; }

        public SynchronizationMap(DataResponse response, FrameGroup group)
        {
            Response = response;
            SyncMap = new Dictionary<UInt64, Synchronization>();

            int count = response.Reader.ReadInt32();
            List<SyncEvent> events = new List<SyncEvent>(count);
            for (int i = 0; i < count; ++i)
                events.Add(new SyncEvent(response.Reader));

            events.Sort();

            for (int i = 0; i < count; ++i)
            {
                SyncEvent scEvent = events[i];

                if (scEvent.OldThreadID != 0)
                {
                    Synchronization oldSync = null;
                    if (!SyncMap.TryGetValue(scEvent.OldThreadID, out oldSync))
                    {
                        oldSync = new Synchronization();
                        SyncMap.Add(scEvent.OldThreadID, oldSync);
                    }

                    if (oldSync.Count > 0)
                    {
                        SyncInterval interval = oldSync[oldSync.Count - 1];
                        interval.Reason = scEvent.Reason;
                        interval.Finish = scEvent.Timestamp.Start;
                        interval.NewThreadId = scEvent.NewThreadID;
                    }
                }

                if (scEvent.NewThreadID != 0)
                {
                    Synchronization newSync = null;
                    if (!SyncMap.TryGetValue(scEvent.NewThreadID, out newSync))
                    {
                        newSync = new Synchronization();
                        SyncMap.Add(scEvent.NewThreadID, newSync);
                    }

                    SyncInterval data = new SyncInterval()
                    {
                        Start = scEvent.Timestamp.Start,
                        Finish = long.MaxValue,
                        Core = scEvent.CPUID,
                    };

                    while (newSync.Count > 0)
                    {
                        SyncInterval previous = newSync[newSync.Count - 1];
                        if (previous.Finish <= data.Start)
                            break;

                        newSync.RemoveAt(newSync.Count - 1);
                    }

                    newSync.Add(data);
                }
            }

        }
    }


    public class Synchronization : List<SyncInterval>
    {
        public bool IsWait { get; set; }
    }


	public class FiberSyncInterval : Durable
	{
		public UInt64 threadId { get; set; }

		public static FiberSyncInterval Read(DataResponse response)
		{
			FiberSyncInterval interval = new FiberSyncInterval();
			interval.ReadDurable(response.Reader);

			interval.threadId = response.Reader.ReadUInt64();

			return interval;
		}
	}


	public class FiberSynchronization : IResponseHolder
	{
		public override DataResponse Response { get; set; }
		public int FiberIndex { get; set; }
		public FrameGroup Group { get; set; }

		public List<FiberSyncInterval> Intervals { get; set; }

		public FiberSynchronization(DataResponse response, FrameGroup group)
		{
			Group = group;
			Response = response;
			FiberIndex = response.Reader.ReadInt32();

			int count = response.Reader.ReadInt32();
			Intervals = new List<FiberSyncInterval>(count);

			for (int i = 0; i < count; ++i)
			{
				Intervals.Add(FiberSyncInterval.Read(response));
			}
		}
	}
}
