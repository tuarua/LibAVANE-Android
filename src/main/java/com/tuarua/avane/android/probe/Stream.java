package com.tuarua.avane.android.probe;

import java.util.ArrayList;
import java.util.HashMap;

/**
 * Created by Eoin Landy on 08/10/2016.
 */

public class Stream {
    //Common
    public int index;
    public String id;
    public String codecName;
    public String codecLongName;
    public String profile;
    public String codecType;
    public String codecTimeBase;
    public String codecTagString;
    public int codecTag;

    public Double duration;
    public Double durationTimestamp;

    public Double realFrameRate;
    public Double averageFrameRate;
    public String timeBase;

    public Double startPTS;
    public Double startTime;

    public Double bitRate;
    public Double maxBitRate;
    public Double bitsPerRawSample;
    public Double numFrames;
    public HashMap<String,String> tags = new HashMap<>();
    public ArrayList<HashMap<String, String>> disposition;//TODO create Disposition Object
}
