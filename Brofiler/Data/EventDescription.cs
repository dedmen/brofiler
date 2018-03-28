﻿using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Runtime.Serialization;
using System.Text;
using System.Windows.Media;

namespace Profiler.Data
{
    public class FileLine
    {
        public FileLine(String file, int line)
        {
            File = file;
            Line = line;

            if (!String.IsNullOrEmpty(File))
            {
                int index = File.LastIndexOfAny(new char[] { '\\', '/' });
                ShortName = index != -1 ? File.Substring(index + 1) : File;
                ShortPath = String.Format("{0}:{1}", File, Line);
            }
        }

        public String File { get; private set; }
        public int Line { get; private set; }
        public String ShortName { get; private set; }
        public String ShortPath { get; private set; }

        public override string ToString()
        {
            return ShortPath;
        }

        public static FileLine Empty = new FileLine(String.Empty, 0);
    }

    public class EventDescription : Description
    {
        private bool isSampling = false;
        public bool IsSampling
        {
            get { return isSampling; }
            set
            {
                if (isSampling != value)
                {
                    ProfilerClient.Get().SendMessage(new TurnSamplingMessage(id, value));
                    isSampling = value;
                }
            }
        }

        private int id;
        public string sourceCode;
        private Color forceColor;

        public Color Color { get; private set; }
        public Color ForceColor
        {
            get
            {
                if (forceColor.A == 0)
                {
                    if (Color.A > 0)
                    {
                        forceColor = Color;
                    }
                    else
                    {
                        Random rnd = new Random(FullName.GetHashCode());
                        forceColor = Color.FromRgb((byte)rnd.Next(), (byte)rnd.Next(), (byte)rnd.Next());
                    }
                }
                return forceColor;
            }
        }
        public Brush Brush { get; private set; }

        public bool IsSleep { get { return Color == Colors.White; } }

        public EventDescription() { }
        public EventDescription(String name, int id)
        {
            FullName = name;
            this.id = id;
        }

        const byte IS_SAMPLING_FLAG = 0x1;

		public void SetOverrideColor(Color color)
		{
			Color = color;
			Brush = new SolidColorBrush(color);
		}

        static public EventDescription Read(BinaryReader reader, int id)
        {
            EventDescription desc = new EventDescription();
            int nameLength = reader.ReadInt32();
            desc.FullName = new String(reader.ReadChars(nameLength));
            desc.id = id;

            int fileLength = reader.ReadInt32();
            desc.Path = new FileLine(new String(reader.ReadChars(fileLength)), reader.ReadInt32());

            UInt32 color = reader.ReadUInt32();
            desc.Color = Color.FromArgb((byte)(color >> 24),
                                        (byte)(color >> 16),
                                        (byte)(color >> 8),
                                        (byte)(color));

            desc.Brush = new SolidColorBrush(desc.Color);

            byte flags = reader.ReadByte();
            desc.isSampling = (flags & IS_SAMPLING_FLAG) != 0;

            int sourceLength = reader.ReadInt32();
            if (sourceLength > 0)
                desc.sourceCode = System.Text.Encoding.UTF8.GetString(reader.ReadBytes(sourceLength));

            return desc;
        }

        public override Object GetSharedKey()
        {
            return this;
        }
    }

	public class FiberDescription
	{
		public UInt64 fiberID { get; set; }

		public static FiberDescription Read(DataResponse response)
		{
			BinaryReader reader = response.Reader;
			FiberDescription res = new FiberDescription();
			res.fiberID = reader.ReadUInt64();
			return res;
		}
	}

    public class ThreadDescription
    {
        public String Name { get; set; }
        public UInt64 ThreadID { get; set; }
        public int MaxDepth { get; set; }

        public static ThreadDescription Read(DataResponse response)
        {
            BinaryReader reader = response.Reader;
            ThreadDescription res = new ThreadDescription();

            res.ThreadID = reader.ReadUInt64();
            int nameLength = reader.ReadInt32();
            res.Name = new String(reader.ReadChars(nameLength));
            res.MaxDepth = 1; // TODO: reader.ReadInt32();
            return res;
        }
    }

	public class EventDescriptionBoard : IResponseHolder
    {
        public Stream BaseStream { get; private set; }
        public int ID { get; private set; }
        public Int64 Frequency { get; private set; }
        public Durable TimeSlice { get; private set; }
        public int MainThreadIndex { get; private set; }

        public List<ThreadDescription> Threads { get; private set; }
        public Dictionary<UInt64, int> ThreadID2ThreadIndex { get; private set; }
		public List<FiberDescription> Fibers { get; private set; }

        private List<EventDescription> board = new List<EventDescription>();
        public List<EventDescription> Board
        {
            get { return board; }
        }
        public EventDescriptionBoard() { }

        public EventDescription this[int pos]
        {
            get
            {
                return board[pos];
            }
        }

        public static EventDescriptionBoard Read(DataResponse response)
        {
            BinaryReader reader = response.Reader;

            EventDescriptionBoard desc = new EventDescriptionBoard() { Response = response };
            desc.BaseStream = reader.BaseStream;
            desc.ID = reader.ReadInt32();

            desc.Frequency = reader.ReadInt64();
            Durable.InitFrequency(desc.Frequency);

            desc.TimeSlice = new Durable();
            desc.TimeSlice.ReadDurable(reader);

            int threadCount = reader.ReadInt32();
            desc.Threads = new List<ThreadDescription>(threadCount);
            desc.ThreadID2ThreadIndex = new Dictionary<UInt64, int>();

            for (int i = 0; i < threadCount; ++i)
            {
                ThreadDescription threadDesc = ThreadDescription.Read(response);
                desc.Threads.Add(threadDesc);

                if (!desc.ThreadID2ThreadIndex.ContainsKey(threadDesc.ThreadID))
                {
                    desc.ThreadID2ThreadIndex.Add(threadDesc.ThreadID, i);
                }
                else
                {
                    // The old thread was finished and the new thread was started 
                    // with the same threadID during one profiling session.
                    // Can't do much here - lets show information for the new thread only.
                    desc.ThreadID2ThreadIndex[threadDesc.ThreadID] = i;
                }
            }

            int fibersCount = reader.ReadInt32();
			desc.Fibers = new List<FiberDescription>(fibersCount);
            for (int i = 0; i < fibersCount; ++i)
            {
				FiberDescription fiberDesc = FiberDescription.Read(response);
				desc.Fibers.Add(fiberDesc);
            }

            desc.MainThreadIndex = reader.ReadInt32();

            int count = reader.ReadInt32();
            for (int i = 0; i < count; ++i)
            {
                desc.board.Add(EventDescription.Read(reader, i));
            }
            return desc;
        }

		public override DataResponse Response { get; set; }
    }

    public class Entry : EventData, IComparable<Entry>
    {
        public byte additionalDataType = 0;
        public string sourceCode;
        public string thisArgs;
        public EventDescription Description { get; private set; }

        Entry() { }

        public Entry(EventDescription desc, long start, long finish) : base(start, finish)
        {
            this.Description = desc;
        }

		public void SetOverrideColor(Color color)
		{
			Description.SetOverrideColor(color);
		}

        public static Entry Read(BinaryReader reader, EventDescriptionBoard board)
        {
            Entry res = new Entry();
            res.ReadEventData(reader);

            int descriptionID = reader.ReadInt32();
            res.Description = board[descriptionID];

            res.additionalDataType = reader.ReadByte();
            if (res.additionalDataType == 1)
            {
                int sourceLength = reader.ReadInt32();
                res.sourceCode = System.Text.Encoding.UTF8.GetString(reader.ReadBytes(sourceLength));
            }

            int thisArgsLength = reader.ReadInt32();
            res.thisArgs = System.Text.Encoding.UTF8.GetString(reader.ReadBytes(thisArgsLength));


            return res;
        }

        public int CompareTo(Entry other)
        {
            if (other.Start != Start)
                return Start < other.Start ? -1 : 1;
            else
                return Finish == other.Finish ? 0 : Finish > other.Finish ? -1 : 1;
        }
    }
}
