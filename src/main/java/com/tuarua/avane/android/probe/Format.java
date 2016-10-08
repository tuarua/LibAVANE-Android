package com.tuarua.avane.android.probe;

import java.util.HashMap;

/**
 * Created by Eoin Landy on 08/10/2016.
 */

public class Format {
    public String filename;
    public int numStreams;
    public int numPrograms;
    public String formatName;
    public String formatLongName;
    public Double startTime;
    public Double duration;
    public int size; //bytes
    public int bitRate;
    public int probeScore;
    public HashMap<String,String> tags = new HashMap<>();//hasmap ?
}
