#!/usr/bin/env python3
"""Compile actual Renderer API and exercise callback lifecycle on JVM; no JNI/device execution."""
import argparse, os, pathlib, shutil, subprocess, tempfile, zipfile
p=argparse.ArgumentParser();p.add_argument('--android-jar',required=True);p.add_argument('--baseline-aar',required=True);p.add_argument('--annotation-jar',required=True);a=p.parse_args()
root=pathlib.Path(__file__).resolve().parents[1]
jhome=os.environ.get('JAVA_HOME');javac=str(pathlib.Path(jhome)/'bin/javac') if jhome else shutil.which('javac');java=str(pathlib.Path(jhome)/'bin/java') if jhome else shutil.which('java')
with tempfile.TemporaryDirectory(prefix='viro-capture-pose-test-') as temp:
 d=pathlib.Path(temp);classes=d/'classes';classes.mkdir();baseline=d/'baseline.jar'
 with zipfile.ZipFile(a.baseline_aar) as z:baseline.write_bytes(z.read('classes.jar'))
 cp=os.pathsep.join([a.android_jar,str(baseline),a.annotation_jar])
 subprocess.run([javac,'-proc:none','-cp',cp,'-d',str(classes),str(root/'android/sharedCode/src/main/java/com/viro/core/Renderer.java')],check=True)
 files={
 'android/os/Looper.java':'package android.os; public class Looper {public static Looper getMainLooper(){return new Looper();}}',
 'android/os/Handler.java':'package android.os; public class Handler {public Handler(Looper l){} public boolean post(Runnable r){r.run();return true;} public boolean postDelayed(Runnable r,long t){return true;} public void removeCallbacks(Runnable r){}}',
 'com/viro/core/CaptureHeadPoseLifecycleTest.java':r'''package com.viro.core;
import java.lang.reflect.*;import java.util.*;
public class CaptureHeadPoseLifecycleTest {
 static void set(Renderer r,String key,Object v)throws Exception{Field f=Renderer.class.getDeclaredField(key);f.setAccessible(true);f.set(r,v);}
 static long pending(Renderer r)throws Exception{Field f=Renderer.class.getDeclaredField("mCapturePosePending");f.setAccessible(true);return f.getLong(r);}
 static void finish(Renderer r,long id,int status,boolean nativeDone)throws Exception{Method m=Renderer.class.getDeclaredMethod("finishCaptureHeadPose",long.class,int.class,long.class,long.class,long.class,int.class,float[].class,boolean.class);m.setAccessible(true);m.invoke(r,id,status,99L,7L,15L,0,new float[]{1,2,3,0,0,0,1},nativeDone);}
 static Renderer admitted(List<Renderer.CaptureHeadPoseResult> results)throws Exception{Renderer r=new Renderer();set(r,"mCapturePosePending",7L);set(r,"mCapturePoseDomain",Renderer.CaptureClockDomain.EXTERNALLY_VERIFIED_CLOCK_MONOTONIC);set(r,"mCapturePoseTimestamp",123L);set(r,"mCapturePoseCallback",(Renderer.CaptureHeadPoseCallback)results::add);set(r,"mCapturePoseTimeout",(Runnable)()->{});return r;}
 static void require(boolean v,String why){if(!v)throw new AssertionError(why);}
 public static void main(String[] args)throws Exception{
  List<Renderer.CaptureHeadPoseResult> out=new ArrayList<>();Renderer r=admitted(out);
  finish(r,8L,0,true);require(out.isEmpty()&&pending(r)==7,"stale callback altered request");
  finish(r,7L,9,false);require(out.size()==1&&out.get(0).status==Renderer.CaptureHeadPoseStatus.TIMEOUT&&out.get(0).pose==null&&pending(r)==7,"timeout must preserve native queue slot and remove pose");
  finish(r,7L,0,true);require(out.size()==1&&pending(r)==0,"late native completion duplicated callback or failed to release slot");
  out.clear();r=admitted(out);set(r,"mCapturePoseDestroyed",true);finish(r,7L,0,true);require(out.size()==1&&out.get(0).status==Renderer.CaptureHeadPoseStatus.DESTROYED&&out.get(0).pose==null,"destroy race delivered valid pose");
  out.clear();r=admitted(out);r.destroy();require(out.size()==1&&out.get(0).status==Renderer.CaptureHeadPoseStatus.DESTROYED,"destroy failed to settle pending callback");
  require(r.getCaptureHeadPoseSpaceGeneration()==0,"destroyed reference bootstrap should be unavailable");
  Renderer idle=new Renderer();out.clear();idle.requestCaptureHeadPose(123,7,Renderer.CaptureClockDomain.UNKNOWN,out::add);require(out.size()==1&&out.get(0).status==Renderer.CaptureHeadPoseStatus.NOT_READY&&out.get(0).pose==null,"missing native handle must settle without JNI");
  System.out.println("PASS: stale, timeout, late completion, destroy race, destroy settlement, unavailable bootstrap/handle (host JVM only)");
 }
}'''}
 sources=[]
 for name,code in files.items():f=d/name;f.parent.mkdir(parents=True,exist_ok=True);f.write_text(code);sources.append(str(f))
 subprocess.run([javac,'-proc:none','-cp',str(classes)+os.pathsep+cp,'-d',str(classes),*sources],check=True)
 subprocess.run([java,'-cp',str(classes)+os.pathsep+cp,'com.viro.core.CaptureHeadPoseLifecycleTest'],check=True)
