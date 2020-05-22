﻿using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Net;
using UnityEngine;

public enum PrepareState
{
    Unprepared,
    Preparing,
    Prepared
}

public class KinectReceiver
{
    private const float HEARTBEAT_INTERVAL_SEC = 1.0f;
    private const float HEARTBEAT_TIME_OUT_SEC = 5.0f;

    private int receiverSessionId;
    private IPEndPoint senderEndPoint;
    private KinectOrigin kinectOrigin;
    private VideoMessageAssembler videoMessageAssembler;
    private AudioPacketReceiver audioPacketReceiver;
    private TextureGroupUpdater textureGroupUpdater;
    private Stopwatch heartbeatStopWatch;
    private Stopwatch receivedAnyStopWatch;

    public int ReceiverSessionId => receiverSessionId;
    public IPEndPoint SenderEndPoint => senderEndPoint;
    public KinectOrigin KinectOrigin => kinectOrigin;
    public TextureGroupUpdater TextureGroupUpdater => textureGroupUpdater;

    public PrepareState State
    {
        get
        {
            if (kinectOrigin.Screen.State == PrepareState.Unprepared || TextureGroupUpdater.State == PrepareState.Unprepared)
                return PrepareState.Unprepared;

            if (kinectOrigin.Screen.State == PrepareState.Prepared && TextureGroupUpdater.State == PrepareState.Prepared)
                return PrepareState.Prepared;

            return PrepareState.Preparing;
        }
    }

    public KinectReceiver(int receiverSessionId, IPEndPoint senderEndPoint, KinectOrigin kinectOrigin)
    {
        this.receiverSessionId = receiverSessionId;
        this.senderEndPoint = senderEndPoint;
        this.kinectOrigin = kinectOrigin;
        videoMessageAssembler = new VideoMessageAssembler(receiverSessionId, senderEndPoint);
        audioPacketReceiver = new AudioPacketReceiver();
        textureGroupUpdater = new TextureGroupUpdater(kinectOrigin.Screen.Material, receiverSessionId, senderEndPoint);
        heartbeatStopWatch = Stopwatch.StartNew();
        receivedAnyStopWatch = Stopwatch.StartNew();
    }

    public void StartPrepare(MonoBehaviour monoBehaviour, SenderPacketSet senderPacketSet)
    {
        if (kinectOrigin.Screen.State == PrepareState.Unprepared)
        {
            KinectOrigin.Screen.StartPrepare(senderPacketSet.InitPacketDataList[0]);
            TextureGroupUpdater.StartPrepare(monoBehaviour, senderPacketSet.InitPacketDataList[0]);
        }
    }

    public bool UpdateFrame(MonoBehaviour monoBehaviour, UdpSocket udpSocket, SenderPacketSet senderPacketSet)
    {
        var videoMessageList = new List<Tuple<int, VideoSenderMessageData>>();
        try
        {
            if (heartbeatStopWatch.Elapsed.TotalSeconds > HEARTBEAT_INTERVAL_SEC)
            {
                udpSocket.Send(PacketHelper.createHeartbeatReceiverPacketBytes(receiverSessionId), senderEndPoint);
                heartbeatStopWatch = Stopwatch.StartNew();
            }

            if (senderPacketSet.ReceivedAny)
            {
                videoMessageAssembler.Assemble(udpSocket,
                                               senderPacketSet.VideoPacketDataList,
                                               senderPacketSet.FecPacketDataList,
                                               textureGroupUpdater.lastVideoFrameId,
                                               videoMessageList);
                audioPacketReceiver.Receive(senderPacketSet.AudioPacketDataList, kinectOrigin.Speaker.RingBuffer);
                receivedAnyStopWatch = Stopwatch.StartNew();
            }
            else
            {
                if (receivedAnyStopWatch.Elapsed.TotalSeconds > HEARTBEAT_TIME_OUT_SEC)
                {
                    UnityEngine.Debug.Log($"Timed out after waiting for {HEARTBEAT_TIME_OUT_SEC} seconds without a received packet.");
                    return false;
                }
            }
        }
        catch (UdpSocketException e)
        {
            UnityEngine.Debug.Log($"UdpSocketRuntimeError: {e}");
            return false;
        }

        textureGroupUpdater.UpdateFrame(udpSocket, videoMessageList);
        kinectOrigin.UpdateFrame(senderPacketSet.FloorPacketDataList);

        return true;
    }
}
