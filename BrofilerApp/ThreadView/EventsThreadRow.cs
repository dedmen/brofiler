using System;
using System.Collections.Generic;
using System.Windows;
using Profiler.Data;
using System.Windows.Media;
using Profiler.DirectX;
using System.Windows.Forms;

namespace Profiler
{
	public class EventsThreadRow : ThreadRow
	{
		public ThreadDescription Description { get; set; }
		public ThreadData EventData { get; set; }
		int MaxDepth { get; set; }

		public bool IsExpanded { get; set; }

		List<Mesh> Blocks { get; set; }
		List<Mesh> SyncMesh { get; set; }
		List<Mesh> SyncWorkMesh { get; set; }
		DynamicMesh CallstackMeshPolys { get; set; }
		DynamicMesh CallstackMeshLines { get; set; }


		double SyncLineHeight = 4.0 * RenderSettings.dpiScaleY;
		static Color SynchronizationColor = Colors.Magenta;
		static Color SynchronizationColorUser = Colors.OrangeRed;
		static Color[] WorkColors = new Color[8]
			{   Colors.Lime,
				Colors.LimeGreen,
				Colors.ForestGreen,
				Colors.OliveDrab,

				Colors.RoyalBlue,
				Colors.Cyan,
				Colors.SlateBlue,
				Colors.LightBlue,
			};

		double CallstackMarkerRadius = 4.0 * RenderSettings.dpiScaleY;

		struct IntPair
		{
			public int count;
			public long duration;
		}


		bool IsUserInitiatedSync(SyncReason reason)
		{
			if (SyncReason.Win_UserRequest < reason && reason < SyncReason.Win_MaximumWaitReason)
			{
				return false;
			}

			return true;
		}

		static Color CallstackColor = Colors.Red;
		static Color SystemCallstackColor = Colors.Yellow;

		EventFilter Filter { get; set; }
		Mesh FilterMesh;

		public EventsThreadRow(FrameGroup group, ThreadDescription desc, ThreadData data)
		{
			Description = desc;
			EventData = data;
			Group = group;
			MaxDepth = 1;
			IsExpanded = true;

			List<EventNode> rootCategories = new List<EventNode>();
			List<EventNode> nodesToProcess = new List<EventNode>();

			foreach (EventFrame frame in data.Events)
			{
				// Fill holes in timeline from regular events (not categories)
				// ------------------------------------------------------------------------------------------------------
				const double thresholdMs = 0.1;

				EventTree categoriesTree = frame.CategoriesTree;
				rootCategories.Clear();
				foreach (EventNode node in frame.CategoriesTree.Children)
				{
					rootCategories.Add(node);
				}

				if (rootCategories.Count != 0)
				{
					nodesToProcess.Clear();
					foreach (EventNode node in frame.Root.Children)
					{
						nodesToProcess.Add(node);
					}

					while (nodesToProcess.Count > 0)
					{
						EventNode node = nodesToProcess[0];
						nodesToProcess.RemoveAt(0);

						bool nodeIntersectWithCategories = false;

						foreach (EventNode categoryNode in rootCategories)
						{
							// drop nodes less than thresholdMs ms
							if (node.Entry.Duration < thresholdMs)
							{
								nodeIntersectWithCategories = true;
								break;
							}

							// node is entirely inside the categoryNode
							if (node.Entry.Start >= categoryNode.Entry.Start && node.Entry.Finish <= categoryNode.Entry.Finish)
							{
								nodeIntersectWithCategories = true;
								break;
							}

							// node is partially inside the categoryNode
							if (node.Entry.Intersect(categoryNode.Entry))
							{
								foreach (EventNode tmp in node.Children)
								{
									nodesToProcess.Add(tmp);
								}

								nodeIntersectWithCategories = true;
								break;
							}
						}

						if (nodeIntersectWithCategories == false && node.Entry.Duration >= thresholdMs)
						{
							// node is not intersect with any categoryNode (add to category tree)
							EventNode fakeCategoryNode = new EventNode(frame.CategoriesTree, node.Entry);

							node.Entry.SetOverrideColor(GenerateColorFromString(node.Entry.Description.FullName));

							rootCategories.Add(fakeCategoryNode);
							frame.CategoriesTree.Children.Add(fakeCategoryNode);
						}
					}
				}
				// ------------------------------------------------------------------------------------------------------

				MaxDepth = Math.Max(GetTree(frame).Depth, MaxDepth);
			}
		}

		void BuildMeshNode(DirectX.ComplexDynamicMesh builder, ThreadScroll scroll, EventNode node, int level)
		{
			if (level == MaxDepth)
				return;

			Interval interval = scroll.TimeToUnit(node.Entry);

			double y = (double)level / MaxDepth;
			double h = 1.0 / MaxDepth;

			builder.AddRect(new Rect(interval.Left, y, interval.Width, h), node.Description.ForceColor);

			foreach (EventNode child in node.Children)
			{
				BuildMeshNode(builder, scroll, child, level + 1);
			}
		}

		const int DIPSplitCount = 20;

		public override void BuildMesh(DirectX.DirectXCanvas canvas, ThreadScroll scroll)
		{
			// Build Mesh
			DirectX.ComplexDynamicMesh builder = new ComplexDynamicMesh(canvas, DIPSplitCount);
			DirectX.ComplexDynamicMesh syncBuilder = new ComplexDynamicMesh(canvas, DIPSplitCount);
			DirectX.ComplexDynamicMesh syncWorkBuilder = new ComplexDynamicMesh(canvas, DIPSplitCount);

			if (EventData.Sync != null && EventData.Sync != null)
			{
				SyncReason stallReason = SyncReason.SyncReasonCount;
				long stallFrom = 0;
				int frameSyncIndex = 0;

				for (int i = 0; i < EventData.Sync.Count; i++)
				{
					SyncInterval sync = EventData.Sync[i];

					Interval workInterval = scroll.TimeToUnit(sync);

					//draw work
					int coreColorIndex = (int)sync.Core;
					coreColorIndex = coreColorIndex % WorkColors.Length;
					Color WorkColor = WorkColors[coreColorIndex];
					syncWorkBuilder.AddRect(new Rect(workInterval.Left, 0, workInterval.Right - workInterval.Left, SyncLineHeight / Height), WorkColor);

					if (i == 0)
					{
						stallReason = sync.Reason;
						stallFrom = sync.Finish;
						continue;
					}

					long workStart = sync.Start;
					long workFinish = sync.Finish;

					while (frameSyncIndex < EventData.Events.Count && EventData.Events[frameSyncIndex].Finish < stallFrom)
						++frameSyncIndex;

					//Ignoring all the waiting outside marked work to simplify the view
					if (frameSyncIndex < EventData.Events.Count && EventData.Events[frameSyncIndex].Start <= workStart)
					{
						Interval syncInterval = scroll.TimeToUnit(new Durable(stallFrom, workStart));

						double syncWidth = syncInterval.Right - syncInterval.Left;
						if (syncWidth <= 0)
						{
							syncWidth = 0.1;
						}

						// draw sleep
						Color waitColor = IsUserInitiatedSync(stallReason) ? SynchronizationColorUser : SynchronizationColor;
						syncBuilder.AddRect(new Rect(syncInterval.Left, 0, syncWidth, SyncLineHeight / Height), waitColor);
					}

					stallFrom = workFinish;
					stallReason = sync.Reason;
				}
			}

			foreach (EventFrame frame in EventData.Events)
			{
				Durable interval = Group.Board.TimeSlice;
				EventTree tree = GetTree(frame);
				foreach (EventNode node in tree.Children)
				{
					BuildMeshNode(builder, scroll, node, 0);
				}
			}

			Blocks = builder.Freeze(canvas.RenderDevice);
			SyncMesh = syncBuilder.Freeze(canvas.RenderDevice);
			SyncWorkMesh = syncWorkBuilder.Freeze(canvas.RenderDevice);

			CallstackMeshPolys = canvas.CreateMesh();
			CallstackMeshPolys.Projection = Mesh.ProjectionType.Pixel;

			CallstackMeshLines = canvas.CreateMesh();
			CallstackMeshLines.Geometry = Mesh.GeometryType.Lines;
			CallstackMeshLines.Projection = Mesh.ProjectionType.Pixel;
		}

		public override double Height { get { return RenderParams.BaseHeight * MaxDepth; } }
		public override string Name { get { return Description.ThreadID != UInt64.MaxValue ? string.Format("{0} (0x{1:x})", Description.Name, Description.ThreadID) : Description.Name; } }

		double TextDrawThreshold = 8.0 * RenderSettings.dpiScaleX;
		double TextDrawOffset = 1.5 * RenderSettings.dpiScaleY;

		public static void Draw(DirectX.DirectXCanvas canvas, List<Mesh> meshes, Matrix world)
		{
			meshes.ForEach(mesh =>
			{
				mesh.WorldTransform = world;
				canvas.Draw(mesh);
			});
		}

		EventTree GetTree(EventFrame frame)
		{
			return IsExpanded ? frame.Root : frame.CategoriesTree;
		}

		public override void Render(DirectX.DirectXCanvas canvas, ThreadScroll scroll, DirectXCanvas.Layer layer, Rect box)
		{
			Matrix world = new Matrix(scroll.Zoom, 0.0, 0.0, (Height - 2.0 * RenderParams.BaseMargin) / scroll.Height,
									  -(scroll.ViewUnit.Left * scroll.Zoom),
									  (Offset + 1.0 * RenderParams.BaseMargin) / scroll.Height);

			if (layer == DirectXCanvas.Layer.Background)
			{
				Draw(canvas, Blocks, world);

				if (FilterMesh != null)
				{
					FilterMesh.WorldTransform = world;
					canvas.Draw(FilterMesh);
				}

				if (scroll.SyncDraw == ThreadScroll.SyncDrawType.Wait)
				{
					Draw(canvas, SyncMesh, world);
				}

				if (SyncWorkMesh != null && scroll.SyncDraw == ThreadScroll.SyncDrawType.Work)
				{
					Draw(canvas, SyncWorkMesh, world);
				}

				Data.Utils.ForEachInsideInterval(EventData.Events, scroll.ViewTime, frame =>
				{
					GetTree(frame).ForEachChild((node, level) =>
					{
						Entry entry = (node as EventNode).Entry;
						Interval intervalPx = scroll.TimeToPixel(entry);

						if (intervalPx.Width < TextDrawThreshold || intervalPx.Right < 0.0)
							return false;

						if (intervalPx.Left < 0.0)
						{
							intervalPx.Width += intervalPx.Left;
							intervalPx.Left = 0.0;
						}

						double lum = DirectX.Utils.GetLuminance(entry.Description.ForceColor);
						Color color = lum < DirectX.Utils.LuminanceThreshold ? Colors.White : Colors.Black;

						canvas.Text.Draw(new Point(intervalPx.Left + TextDrawOffset, Offset + level * RenderParams.BaseHeight),
										 entry.Description.Name,
										 color,
										 TextAlignment.Left,
										 intervalPx.Width - TextDrawOffset);

						return true;
					});
				});
			}

			if (layer == DirectXCanvas.Layer.Foreground)
			{
				if (CallstackMeshPolys != null && CallstackMeshLines != null && scroll.DrawCallstacks)
				{
					double width = CallstackMarkerRadius;
					double height = CallstackMarkerRadius;
					double offset = Offset + RenderParams.BaseHeight * 0.5;

					Data.Utils.ForEachInsideInterval(EventData.Callstacks, scroll.ViewTime, callstack =>
					{
						double center = scroll.TimeToPixel(callstack);

						Point[] points = new Point[] { new Point(center - width, offset), new Point(center, offset - height), new Point(center + width, offset), new Point(center, offset + height) };

						Color fillColor = (callstack.Reason == CallStackReason.AutoSample) ? CallstackColor : SystemCallstackColor;
						Color strokeColor = Colors.Black;

						CallstackMeshPolys.AddRect(points, fillColor);
						CallstackMeshLines.AddRect(points, strokeColor);
					});

					CallstackMeshPolys.Update(canvas.RenderDevice);
					CallstackMeshLines.Update(canvas.RenderDevice);

					//CallstackMeshPolys.World = world;
					//CallstackMeshLines.World = world;

					canvas.Draw(CallstackMeshPolys);
					canvas.Draw(CallstackMeshLines);
				}
			}


		}

		public delegate void EventNodeHoverHandler(Point mousePos, Rect rect, ThreadRow row, EventNode node);
		public event EventNodeHoverHandler EventNodeHover;

		public delegate void EventNodeSelectedHandler(ThreadRow row, EventFrame frame, EventNode node);
		public event EventNodeSelectedHandler EventNodeSelected;

		public EventFrame FindFrame(ITick tick)
		{
			int index = Data.Utils.BinarySearchExactIndex(EventData.Events, tick.Start);
			if (index >= 0)
			{
				return EventData.Events[index];
			}
			return null;
		}

		public int FindNode(Point point, ThreadScroll scroll, out EventFrame eventFrame, out EventNode eventNode)
		{
			ITick tick = scroll.PixelToTime(point.X);

			int index = Data.Utils.BinarySearchExactIndex(EventData.Events, tick.Start);

			EventFrame resultFrame = null;
			EventNode resultNode = null;
			int resultLevel = -1;

			if (index >= 0)
			{
				EventFrame frame = EventData.Events[index];

				int desiredLevel = (int)(point.Y / RenderParams.BaseHeight);

				GetTree(frame).ForEachChild((node, level) =>
				{
					if (level > desiredLevel || resultFrame != null)
					{
						return false;
					}

					if (level == desiredLevel)
					{
						EventNode evNode = (node as EventNode);
						if (evNode.Entry.Intersect(tick.Start))
						{
							resultFrame = frame;
							resultNode = evNode;
							resultLevel = level;
							return false;
						}
					}

					return true;
				});
			}

			eventFrame = resultFrame;
			eventNode = resultNode;
			return resultLevel;
		}

		public override void OnMouseMove(Point point, ThreadScroll scroll)
		{
			EventNode node = null;
			EventFrame frame = null;
			int level = FindNode(point, scroll, out frame, out node);
			if (level != -1)
			{
				Interval interval = scroll.TimeToPixel(node.Entry);
				Rect rect = new Rect(interval.Left, Offset + level * RenderParams.BaseHeight + RenderParams.BaseMargin, interval.Width, RenderParams.BaseHeight - RenderParams.BaseMargin);
				EventNodeHover(point, rect, this, node);
			}
			else
			{
				EventNodeHover(point, new Rect(), this, null);
			}
		}

		public override void OnMouseHover(Point point, ThreadScroll scroll, List<object> dataContext)
		{
			EventNode node = null;
			EventFrame frame = null;

			ITick tick = scroll.PixelToTime(point.X);

			if (FindNode(point, scroll, out frame, out node) != -1)
			{
				dataContext.Add(node);
			}

            // show current sync info
		    if (node != null)
		    {


		        if (!String.IsNullOrEmpty(node.Entry.thisArgs))
		        {
		            ThisArgs interval = new ThisArgs() { args = node.Entry.thisArgs };


		            dataContext.Add(interval);

		        }






                // build all intervals inside selected node
                int from = Data.Utils.BinarySearchClosestIndex(frame.Synchronization, node.Entry.Start);
				int to = Data.Utils.BinarySearchClosestIndex(frame.Synchronization, node.Entry.Finish);

				if (from >= 0 && to >= from)
				{
					IntPair[] waitInfo = new IntPair[(int)SyncReason.SyncReasonCount];

					for (int index = from; index <= to; ++index)
					{
						SyncReason reason = frame.Synchronization[index].Reason;
						int reasonIndex = (int)reason;

						long idleStart = frame.Synchronization[index].Finish;
						long idleFinish = (index + 1 < frame.Synchronization.Count) ? frame.Synchronization[index + 1].Start : frame.Finish;

						if (idleStart > node.Entry.Finish)
						{
							continue;
						}

						long idleStartClamped = Math.Max(idleStart, node.Entry.Start);
						long idleFinishClamped = Math.Min(idleFinish, node.Entry.Finish);
						long durationInTicks = idleFinishClamped - idleStartClamped;
						waitInfo[reasonIndex].duration += durationInTicks;
						waitInfo[reasonIndex].count++;
					}

					NodeWaitIntervalList intervals = new NodeWaitIntervalList();

					for (int i = 0; i < waitInfo.Length; i++)
					{
						if (waitInfo[i].count > 0)
						{
							NodeWaitInterval interval = new NodeWaitInterval() { Start = 0, Finish = waitInfo[i].duration, Reason = (SyncReason)i, NodeInterval = node.Entry, Count = waitInfo[i].count };
							intervals.Add(interval);
						}
					}

					intervals.Sort((a, b) =>
				   {
					   return Comparer<long>.Default.Compare(b.Finish, a.Finish);
				   });

					if (intervals.Count > 0)
						dataContext.Add(intervals);
				}
			} // FindNode



			if (scroll.DrawCallstacks && EventData.Callstacks != null)
			{
				int startIndex = Data.Utils.BinarySearchClosestIndex(EventData.Callstacks, tick.Start);

				for (int i = startIndex; (i <= startIndex + 1) && (i < EventData.Callstacks.Count) && (i != -1); ++i)
				{
					double pixelPos = scroll.TimeToPixel(EventData.Callstacks[i]);
					if (Math.Abs(pixelPos - point.X) < CallstackMarkerRadius * 1.2)
					{
						//if (EventData.Callstacks[i].Reason < CallStackReason.MaxReasonsCount)
						//{
						//	dataContext.Add("\nSyscall : " + EventData.Callstacks[i].Reason.ToString());
						//}

						dataContext.Add(EventData.Callstacks[i]);
						break;
					}
				}
			}
		}

		public override void OnMouseClick(Point point, ThreadScroll scroll)
		{
			EventNode node = null;
			EventFrame frame = null;
			int level = FindNode(point, scroll, out frame, out node);
			if (EventNodeSelected != null)
			{
				ITick tick = scroll.PixelToTime(point.X);
				if (frame == null)
				{
					frame = FindFrame(tick);
				}
				if (frame != null)
				{
					EventNodeSelected(this, frame, node);
				}
			}
		}

		//static Color FilterFrameColor = Colors.LimeGreen;
		//static Color FilterEntryColor = Colors.Tomato;

		static Color FilterFrameColor = Colors.PaleGreen;
		static Color FilterEntryColor = Colors.Salmon;


		static Color GenerateColorFromString(string s)
		{
			int hash = s.GetHashCode();

			byte r = (byte)((hash & 0xFF0000) >> 16);
			byte g = (byte)((hash & 0x00FF00) >> 8);
			byte b = (byte)(hash & 0x0000FF);

			return Color.FromArgb(0xFF, r, g, b);
		}

		public override void ApplyFilter(DirectXCanvas canvas, ThreadScroll scroll, HashSet<EventDescription> descriptions)
		{
			Filter = EventFilter.Create(EventData, descriptions);

			if (Filter != null)
			{
				DynamicMesh builder = canvas.CreateMesh();

				foreach (EventFrame frame in EventData.Events)
				{
					Interval interval = scroll.TimeToUnit(frame.Header);
					builder.AddRect(new Rect(interval.Left, 0.0, interval.Width, 1.0), FilterFrameColor);
				}

				foreach (Entry entry in Filter.Entries)
				{
					Interval interval = scroll.TimeToUnit(entry);
					builder.AddRect(new Rect(interval.Left, 0.0, interval.Width, 1.0), FilterEntryColor);
				}

				SharpDX.Utilities.Dispose(ref FilterMesh);
				FilterMesh = builder.Freeze(canvas.RenderDevice);
				//if (FilterMesh != null)
				//    FilterMesh.UseAlpha = true;
			}
			else
			{
				SharpDX.Utilities.Dispose(ref FilterMesh);
			}
		}
	}
}