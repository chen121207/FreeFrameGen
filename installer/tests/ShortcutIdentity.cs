using System;
using System.IO;
using System.Reflection;
using System.Collections.Generic;

// Isolated contract test: no installation, registration or real Start menu changes.
internal static class Product
{
    internal const string Id="FFG-Shortcut-Contract-Test", Folder="FFGShortcutTest", DisplayName=Folder;
    internal const string Version="0.4.0", Description="Test", Exe="unused.exe", Url="", LaunchArgs="";
    internal static Dictionary<string,string> Manifest(){return new Dictionary<string,string>();}
}
internal static class ShortcutIdentityTests
{
    [STAThread]
    public static int Main(string[] args)
    {
        string work=Path.GetFullPath(args[0]);
        if(Directory.Exists(work))throw new IOException("Test directory already exists");
        Directory.CreateDirectory(work);
        string target=Path.Combine(work,"fixture.txt"), link=Path.Combine(work,"fixture.lnk");
        File.WriteAllText(target,"owned fixture");
        SetupProgram.Shortcut(link,target,"--original",work);
        FileRecord state=SetupProgram.ReadShortcut(link);
        if(!SetupProgram.OwnedShortcut(state))throw new Exception("Original identity not recognized");
        using(FileStream stream=new FileStream(link,FileMode.Append)){stream.WriteByte(0);}
        if(SetupProgram.Hash(link)==state.Hash)throw new Exception("Test did not change metadata bytes");
        if(!SetupProgram.OwnedShortcut(state))throw new Exception("Metadata-only link wrongly orphaned");
        Type type=Type.GetTypeFromProgID("WScript.Shell",true);
        object shell=Activator.CreateInstance(type), shortcut=null;
        try{
            shortcut=type.InvokeMember("CreateShortcut",BindingFlags.InvokeMethod,null,shell,new object[]{link});
            Type lt=shortcut.GetType();
            lt.InvokeMember("Arguments",BindingFlags.SetProperty,null,shortcut,new object[]{"--custom"});
            lt.InvokeMember("Save",BindingFlags.InvokeMethod,null,shortcut,null);
        }finally{
            if(shortcut!=null)System.Runtime.InteropServices.Marshal.FinalReleaseComObject(shortcut);
            System.Runtime.InteropServices.Marshal.FinalReleaseComObject(shell);
        }
        if(SetupProgram.OwnedShortcut(state))throw new Exception("Customized arguments would be deleted");
        // Exact fixture-only cleanup, no recursive delete.
        File.Delete(link);File.Delete(target);Directory.Delete(work,false);
        Console.WriteLine("PASS: original identity, metadata-only changes, customized-argument retention");
        return 0;
    }
}
