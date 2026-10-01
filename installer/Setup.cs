using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.IO;
using System.IO.Compression;
using System.Reflection;
using System.Security.Cryptography;
using System.Threading;
using System.Windows.Forms;
using System.Xml.Serialization;
using Microsoft.Win32;

[assembly: AssemblyVersion(Product.Version + ".0")]
[assembly: AssemblyFileVersion(Product.Version + ".0")]

public sealed class FileRecord
{
    public string Path;
    public string Hash;
    public string Target, Arguments, WorkingDirectory, Description, IconLocation, Hotkey;
    public int WindowStyle;
    public uint ShellLinkFlags;
}
public sealed class InstallState
{
    public string ProductId;
    public string Version;
    public string Root;
    public string UninstallerHash;
    public List<FileRecord> Shortcuts = new List<FileRecord>();
}
public sealed class Options
{
    public bool Silent, Uninstall, Desktop;
    public string Destination, LogPath, Preview;
    public int ParentPid;
    public static Options Parse(string[] args)
    {
        Options o = new Options();
        for (int i = 0; i < args.Length; ++i)
        {
            string a = args[i];
            if (a == "--silent") o.Silent = true;
            else if (a == "--uninstall") o.Uninstall = true;
            else if (a == "--install") o.Uninstall = false;
            else if (a == "--desktop") o.Desktop = true;
            else if (a == "--no-desktop") o.Desktop = false;
            else if (a == "--dir" && i + 1 < args.Length) o.Destination = args[++i];
            else if (a == "--log" && i + 1 < args.Length) o.LogPath = args[++i];
            else if (a == "--preview" && i + 1 < args.Length) o.Preview = args[++i];
            else if (a == "--parent" && i + 1 < args.Length) o.ParentPid = int.Parse(args[++i]);
            else throw new ArgumentException("未知参数 / Unknown option: " + a);
        }
        return o;
    }
}
internal static class SetupProgram
{
    internal const string StateName = "install-state.xml";
    internal const string UninstallName = "Uninstall.exe";
    internal static string Self { get { return Assembly.GetExecutingAssembly().Location; } }
    internal static string RegistryPath { get { return @"Software\Microsoft\Windows\CurrentVersion\Uninstall\" + Product.Id; } }
    internal static string Group { get { return System.IO.Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.Programs), Product.Folder); } }
    internal static string DataDir { get { return System.IO.Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "GraphicsResearch", Product.Folder); } }
    internal static string DefaultDir { get { return System.IO.Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "Programs", Product.Folder); } }
    internal static readonly Dictionary<string, string> Manifest = Product.Manifest();

    [STAThread]
    private static int Main(string[] args)
    {
        Options o = null;
        try
        {
            o = Options.Parse(args);
            if (string.Equals(System.IO.Path.GetFileName(Self), UninstallName, StringComparison.OrdinalIgnoreCase))
                o.Uninstall = true;
            Application.EnableVisualStyles();
            Application.SetCompatibleTextRenderingDefault(false);
            if (o.Preview != null)
            {
                using (SetupForm f = new SetupForm(o))
                {
                    f.ShowInTaskbar = false;
                    f.StartPosition = FormStartPosition.Manual;
                    f.Location = new Point(-32000, -32000);
                    f.Show();
                    f.PerformLayout();
                    Application.DoEvents();
                    using (Bitmap b = new Bitmap(f.Width, f.Height))
                    {
                        f.DrawToBitmap(b, new Rectangle(0, 0, b.Width, b.Height));
                        b.Save(o.Preview, System.Drawing.Imaging.ImageFormat.Png);
                    }
                    f.Hide();
                }
                return 0;
            }
            if (!Environment.Is64BitOperatingSystem) throw new InvalidOperationException("需要 64 位 Windows。");
            if (o.Uninstall)
            {
                if (o.Destination == null) o.Destination = RegisteredDirectory();
                o.Destination = SafeRoot(o.Destination);
                // Never delete the running executable: relocate an identical worker to TEMP.
                if (Same(Self, System.IO.Path.Combine(o.Destination, UninstallName)))
                {
                    string temp = System.IO.Path.Combine(System.IO.Path.GetTempPath(), Product.Folder + "-uninstall-" + Guid.NewGuid().ToString("N"));
                    Directory.CreateDirectory(temp);
                    string worker = System.IO.Path.Combine(temp, "UninstallWorker.exe");
                    File.Copy(Self, worker, false);
                    string next = "--uninstall --dir " + Quote(o.Destination) + " --parent " + Process.GetCurrentProcess().Id;
                    if (o.Silent) next += " --silent";
                    if (o.LogPath != null) next += " --log " + Quote(o.LogPath);
                    Process.Start(new ProcessStartInfo(worker, next) { UseShellExecute = false, CreateNoWindow = o.Silent });
                    return 0; // Silent callers must wait for RESULT in --log, not just this launcher.
                }
            }
            if (o.ParentPid != 0)
            {
                try { using (Process p = Process.GetProcessById(o.ParentPid)) { if (!p.WaitForExit(30000)) throw new IOException("卸载启动器未退出。"); } }
                catch (ArgumentException) { /* parent already exited */ }
            }
            if (o.Silent)
            {
                Execute(o, delegate(string message) { Log(o, message); });
                Log(o, "RESULT=SUCCESS");
                return 0;
            }
            using (SetupForm form = new SetupForm(o)) Application.Run(form);
            return 0;
        }
        catch (Exception e)
        {
            if (o != null) Log(o, "RESULT=FAILED " + e.Message);
            if (o == null || !o.Silent) MessageBox.Show(e.Message, Product.DisplayName, MessageBoxButtons.OK, MessageBoxIcon.Error);
            return 1;
        }
    }

    internal static string Quote(string s) { return "\"" + s.Replace("\"", "") + "\""; }
    internal static bool Same(string a, string b) { return string.Equals(System.IO.Path.GetFullPath(a).TrimEnd('\\'), System.IO.Path.GetFullPath(b).TrimEnd('\\'), StringComparison.OrdinalIgnoreCase); }
    internal static Icon ProductIcon()
    {
        try { return Icon.ExtractAssociatedIcon(Self); }
        catch { return null; }
    }
    internal static void Log(Options o, string s)
    {
        if (o.LogPath != null) File.AppendAllText(o.LogPath, DateTime.UtcNow.ToString("o") + " " + s + Environment.NewLine);
    }
    internal static RegistryKey UserRegistry() { return RegistryKey.OpenBaseKey(RegistryHive.CurrentUser, RegistryView.Registry64); }
    internal static string RegisteredDirectory()
    {
        using (RegistryKey user = UserRegistry())
        using (RegistryKey key = user.OpenSubKey(RegistryPath))
        {
            if (key == null) throw new IOException("没有找到此软件的安装记录。");
            string root = key.GetValue("InstallLocation") as string;
            if (string.IsNullOrEmpty(root)) throw new IOException("安装记录缺少目录。");
            return root;
        }
    }
    internal static string Hash(string path)
    {
        using (SHA256 h = SHA256.Create())
        using (FileStream s = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.Read))
            return BitConverter.ToString(h.ComputeHash(s)).Replace("-", "");
    }
    internal static void NoReparseAncestors(string full)
    {
        for (string p = full; !string.IsNullOrEmpty(p); p = System.IO.Path.GetDirectoryName(p))
        {
            if ((File.Exists(p) || Directory.Exists(p)) && (File.GetAttributes(p) & FileAttributes.ReparsePoint) != 0)
                throw new IOException("不允许符号链接或目录联接: " + p);
        }
    }
    internal static string SafeRoot(string value)
    {
        if (string.IsNullOrWhiteSpace(value) || !System.IO.Path.IsPathRooted(value) || value.StartsWith(@"\\"))
            throw new IOException("请选择本地绝对路径。");
        string full = System.IO.Path.GetFullPath(value).TrimEnd('\\');
        if (full.Length < 4 || full.Substring(2).Contains(":") || full.Contains("\"") ||
            !string.Equals(System.IO.Path.GetFileName(full), Product.Folder, StringComparison.OrdinalIgnoreCase))
            throw new IOException("安装目录最后一级必须为 " + Product.Folder);
        NoReparseAncestors(full);
        for (string p = full; !string.IsNullOrEmpty(p); p = System.IO.Path.GetDirectoryName(p))
            if (File.Exists(System.IO.Path.Combine(p, "left4dead2.exe")) ||
                string.Equals(System.IO.Path.GetFileName(p), "steamapps", StringComparison.OrdinalIgnoreCase))
                throw new IOException("这是独立 Demo / Runtime，禁止安装到游戏目录。");
        return full;
    }
    internal static string Child(string root, string relative)
    {
        if (string.IsNullOrEmpty(relative) || System.IO.Path.IsPathRooted(relative) || relative.Contains(":"))
            throw new IOException("非法安装路径。");
        string full = System.IO.Path.GetFullPath(System.IO.Path.Combine(root, relative));
        if (!full.StartsWith(root + "\\", StringComparison.OrdinalIgnoreCase)) throw new IOException("路径越界。");
        NoReparseAncestors(full);
        return full;
    }
    internal static void Execute(Options o, Action<string> progress)
    {
        using (Mutex mutex = new Mutex(false, @"Local\GraphicsResearch-" + Product.Id))
        {
            bool acquired = false;
            try
            {
                try { acquired = mutex.WaitOne(0); } catch (AbandonedMutexException) { acquired = true; }
                if (!acquired) throw new IOException("另一个安装或卸载操作正在进行。");
                if (o.Uninstall) Remove(o, progress); else Install(o, progress);
            }
            finally { if (acquired) mutex.ReleaseMutex(); }
        }
    }
    internal static void AssertNotRunning(string root)
    {
        foreach (Process p in Process.GetProcessesByName(System.IO.Path.GetFileNameWithoutExtension(Product.Exe)))
        {
            using (p)
            {
                string executable;
                try { executable = p.MainModule.FileName; }
                catch { throw new IOException("无法确认程序是否正在使用，请先关闭 " + Product.Exe); }
                if (executable.StartsWith(root + "\\", StringComparison.OrdinalIgnoreCase))
                    throw new IOException("请先关闭正在运行的软件，再卸载。");
            }
        }
    }
    internal static void Shortcut(string path, string target, string args, string working)
    {
        if (File.Exists(path)) throw new IOException("快捷方式已存在，不会覆盖: " + path);
        Type type = Type.GetTypeFromProgID("WScript.Shell", true);
        object shell = Activator.CreateInstance(type);
        object link = null;
        try
        {
            link = type.InvokeMember("CreateShortcut", BindingFlags.InvokeMethod, null, shell, new object[] { path });
            Type lt = link.GetType();
            lt.InvokeMember("TargetPath", BindingFlags.SetProperty, null, link, new object[] { target });
            lt.InvokeMember("Arguments", BindingFlags.SetProperty, null, link, new object[] { args });
            lt.InvokeMember("WorkingDirectory", BindingFlags.SetProperty, null, link, new object[] { working });
            lt.InvokeMember("Description", BindingFlags.SetProperty, null, link, new object[] { Product.DisplayName });
            // The capture executable embeds the same icon as the installer. Explicitly setting
            // the location keeps Explorer, Start menu and desktop links visually consistent.
            lt.InvokeMember("IconLocation", BindingFlags.SetProperty, null, link, new object[] { target + ",0" });
            lt.InvokeMember("Save", BindingFlags.InvokeMethod, null, link, null);
        }
        finally
        {
            if (link != null) System.Runtime.InteropServices.Marshal.FinalReleaseComObject(link);
            System.Runtime.InteropServices.Marshal.FinalReleaseComObject(shell);
        }
    }
    internal static void SaveState(string root, InstallState state)
    {
        using (FileStream file = new FileStream(Child(root, StateName), FileMode.CreateNew))
            new XmlSerializer(typeof(InstallState)).Serialize(file, state);
    }
    internal static InstallState ReadState(string root)
    {
        using (FileStream file = File.OpenRead(Child(root, StateName)))
        using (System.Xml.XmlReader reader = System.Xml.XmlReader.Create(file, new System.Xml.XmlReaderSettings
        { DtdProcessing = System.Xml.DtdProcessing.Prohibit, XmlResolver = null }))
            return (InstallState)new XmlSerializer(typeof(InstallState)).Deserialize(reader);
    }
    internal static void Install(Options o, Action<string> progress)
    {
        string root = SafeRoot(o.Destination ?? DefaultDir);
        using (RegistryKey user = UserRegistry())
        using (RegistryKey existing = user.OpenSubKey(RegistryPath))
            if (existing != null) throw new IOException("已经安装，请先卸载旧版。不会覆盖升级。");
        if (File.Exists(root) || Directory.Exists(root)) throw new IOException("目标目录已存在，请选择新目录。");
        NoReparseAncestors(Group);
        if (Directory.Exists(Group)) throw new IOException("开始菜单文件夹已存在，安装已停止。");
        string desktop = System.IO.Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.DesktopDirectory), Product.Folder + ".lnk");
        if (o.Desktop) NoReparseAncestors(desktop);
        if (o.Desktop && File.Exists(desktop)) throw new IOException("桌面快捷方式已存在，不会覆盖。");
        NoReparseAncestors(DataDir);
        string parent = System.IO.Path.GetDirectoryName(root);
        Directory.CreateDirectory(parent);
        string stage = System.IO.Path.Combine(parent, "." + Product.Folder + ".install-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(stage);
        bool moved = false, registered = false, groupCreated = false;
        InstallState state = new InstallState { ProductId = Product.Id, Version = Product.Version, Root = root, UninstallerHash = Hash(Self) };
        try
        {
            progress("正在解压和验证文件…");
            HashSet<string> extracted = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
            using (Stream resource = Assembly.GetExecutingAssembly().GetManifestResourceStream("payload.zip"))
            using (ZipArchive zip = new ZipArchive(resource, ZipArchiveMode.Read))
            {
                foreach (ZipArchiveEntry entry in zip.Entries)
                {
                    string relative = entry.FullName.Replace('/', '\\');
                    string expected;
                    if (!Manifest.TryGetValue(relative, out expected) || !extracted.Add(relative))
                        throw new IOException("包文件清单不匹配。");
                    string output = Child(stage, relative);
                    Directory.CreateDirectory(System.IO.Path.GetDirectoryName(output));
                    using (Stream input = entry.Open())
                    using (FileStream file = new FileStream(output, FileMode.CreateNew)) input.CopyTo(file);
                    if (Hash(output) != expected) throw new IOException("安装文件校验失败: " + relative);
                }
            }
            if (extracted.Count != Manifest.Count) throw new IOException("安装包不完整。");
            File.Copy(Self, Child(stage, UninstallName), false);
            // Recheck immediately before publishing the private staging tree.
            SafeRoot(root);
            if (Directory.Exists(root) || File.Exists(root)) throw new IOException("目标目录在安装过程中被创建，操作中止。");
            Directory.Move(stage, root);
            moved = true;
            Directory.CreateDirectory(DataDir); // User output; deliberately preserved at uninstall.
            Directory.CreateDirectory(Group);
            groupCreated = true;
            string mainLink = System.IO.Path.Combine(Group, Product.Folder + ".lnk");
            Shortcut(mainLink, Child(root, @"bin\" + Product.Exe), Product.LaunchArgs, DataDir);
            state.Shortcuts.Add(ReadShortcut(mainLink));
            string removeLink = System.IO.Path.Combine(Group, "卸载 " + Product.Folder + ".lnk");
            Shortcut(removeLink, Child(root, UninstallName), "--uninstall", root);
            state.Shortcuts.Add(ReadShortcut(removeLink));
            if (o.Desktop)
            {
                Shortcut(desktop, Child(root, @"bin\" + Product.Exe), Product.LaunchArgs, DataDir);
                state.Shortcuts.Add(ReadShortcut(desktop));
            }
            SaveState(root, state);
            progress("正在登记卸载入口…");
            using (RegistryKey user = UserRegistry())
            using (RegistryKey key = user.CreateSubKey(RegistryPath))
            {
                registered = true;
                key.SetValue("DisplayName", Product.DisplayName + " (Prototype)");
                key.SetValue("DisplayVersion", Product.Version);
                key.SetValue("Publisher", "Graphics Research / chen121207");
                key.SetValue("InstallLocation", root);
                key.SetValue("DisplayIcon", Child(root, @"bin\" + Product.Exe));
                key.SetValue("UninstallString", Quote(Child(root, UninstallName)) + " --uninstall");
                key.SetValue("QuietUninstallString", Quote(Child(root, UninstallName)) + " --uninstall --silent");
                key.SetValue("URLInfoAbout", Product.Url);
                key.SetValue("NoModify", 1, RegistryValueKind.DWord);
                key.SetValue("NoRepair", 1, RegistryValueKind.DWord);
                key.SetValue("StateSHA256", Hash(Child(root, StateName)));
            }
            progress("安装完成。可以从开始菜单启动软件，或在 Windows“已安装的应用”中卸载。");
        }
        catch
        {
            // Only undo this operation's known, unchanged files. Never recurse-delete a tree.
            if (registered) using (RegistryKey user = UserRegistry()) user.DeleteSubKeyTree(RegistryPath, false);
            foreach (FileRecord link in state.Shortcuts) DeleteUnchanged(link.Path, link.Hash);
            if (groupCreated) RemoveEmptyDirectory(Group);
            string owned = moved ? root : stage;
            foreach (KeyValuePair<string, string> f in Manifest) DeleteUnchanged(Child(owned, f.Key), f.Value);
            DeleteUnchanged(Child(owned, UninstallName), state.UninstallerHash);
            if (File.Exists(Child(owned, StateName))) File.Delete(Child(owned, StateName));
            RemoveEmptyTree(owned);
            throw;
        }
    }
    internal static void DeleteUnchanged(string path, string hash)
    {
        NoReparseAncestors(path);
        if (File.Exists(path) && Hash(path) == hash) File.Delete(path);
    }
    internal static FileRecord ReadShortcut(string path)
    {
        Type type = Type.GetTypeFromProgID("WScript.Shell", true);
        object shell = Activator.CreateInstance(type), link = null;
        try
        {
            link = type.InvokeMember("CreateShortcut", BindingFlags.InvokeMethod, null, shell, new object[] { path });
            Type lt = link.GetType();
            Func<string, object> get = delegate(string name) {
                return lt.InvokeMember(name, BindingFlags.GetProperty, null, link, null);
            };
            return new FileRecord {
                Path = path, Hash = Hash(path), Target = (string)get("TargetPath"),
                Arguments = (string)get("Arguments"), WorkingDirectory = (string)get("WorkingDirectory"),
                Description = (string)get("Description"), IconLocation = (string)get("IconLocation"),
                Hotkey = (string)get("Hotkey"), WindowStyle = Convert.ToInt32(get("WindowStyle")),
                ShellLinkFlags = BitConverter.ToUInt32(File.ReadAllBytes(path), 20)
            };
        }
        finally
        {
            if (link != null) System.Runtime.InteropServices.Marshal.FinalReleaseComObject(link);
            System.Runtime.InteropServices.Marshal.FinalReleaseComObject(shell);
        }
    }
    internal static bool OwnedShortcut(FileRecord saved)
    {
        NoReparseAncestors(saved.Path);
        if (!File.Exists(saved.Path)) return false;
        if (Hash(saved.Path) == saved.Hash) return true;
        if (string.IsNullOrEmpty(saved.Target)) return false; // Legacy record: retain hash-only policy.
        try
        {
            FileRecord actual = ReadShortcut(saved.Path);
            // Shell tracking/metadata can change .lnk bytes when launched. Preserve any
            // user-visible customization, but don't orphan otherwise identical links.
            return Same(actual.Target, saved.Target) &&
                string.Equals(actual.WorkingDirectory, saved.WorkingDirectory, StringComparison.OrdinalIgnoreCase) &&
                actual.Arguments == saved.Arguments && actual.Description == saved.Description &&
                actual.IconLocation == saved.IconLocation && actual.Hotkey == saved.Hotkey &&
                actual.WindowStyle == saved.WindowStyle && actual.ShellLinkFlags == saved.ShellLinkFlags;
        }
        catch { return false; } // Unreadable or changed link remains user-owned.
    }
    internal static void RemoveEmptyDirectory(string path)
    {
        if (Directory.Exists(path) && Directory.GetFileSystemEntries(path).Length == 0) Directory.Delete(path, false);
    }
    internal static void RemoveEmptyTree(string root)
    {
        if (!Directory.Exists(root)) return;
        NoReparseAncestors(root);
        foreach (string dir in Directory.GetDirectories(root))
            if ((File.GetAttributes(dir) & FileAttributes.ReparsePoint) == 0) RemoveEmptyTree(dir);
        RemoveEmptyDirectory(root);
    }
    internal static void Remove(Options o, Action<string> progress)
    {
        string root = SafeRoot(o.Destination);
        if (!Same(root, RegisteredDirectory())) throw new IOException("目录与安装记录不一致。");
        progress("正在检查安装记录和文件校验值…");
        using (RegistryKey user = UserRegistry())
        using (RegistryKey key = user.OpenSubKey(RegistryPath))
            if (Hash(Child(root, StateName)) != (key.GetValue("StateSHA256") as string))
                throw new IOException("安装记录被修改，停止卸载。");
        InstallState state = ReadState(root);
        if (state.ProductId != Product.Id || state.Version != Product.Version || !Same(state.Root, root) || Hash(Self) != state.UninstallerHash)
            throw new IOException("卸载程序版本或身份不匹配。请使用安装目录中的 Uninstall.exe。");
        AssertNotRunning(root);
        List<KeyValuePair<string, string>> targets = new List<KeyValuePair<string, string>>(Manifest);
        targets.Add(new KeyValuePair<string, string>(UninstallName, state.UninstallerHash));
        foreach (KeyValuePair<string, string> item in targets)
        {
            string path = Child(root, item.Key);
            if (Directory.Exists(path)) throw new IOException("文件被替换为目录: " + path);
            if (File.Exists(path))
            {
                if (Hash(path) != item.Value) throw new IOException("文件已修改，未删除任何程序文件。请先备份并恢复原文件: " + path);
                if ((File.GetAttributes(path) & FileAttributes.ReadOnly) != 0)
                    throw new IOException("文件为只读，停止卸载: " + path);
                using (FileStream unlocked = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.None)) { }
            }
        }
        // State hash protects shortcut destinations. Changed shortcuts are preserved.
        foreach (FileRecord link in state.Shortcuts)
        {
            bool allowed = Same(link.Path, System.IO.Path.Combine(Group, Product.Folder + ".lnk")) ||
                Same(link.Path, System.IO.Path.Combine(Group, "卸载 " + Product.Folder + ".lnk")) ||
                Same(link.Path, System.IO.Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.DesktopDirectory), Product.Folder + ".lnk"));
            if (!allowed) throw new IOException("快捷方式记录越界。");
            NoReparseAncestors(link.Path);
        }
        progress("正在移除未修改的程序文件和卸载入口…");
        // Resolve identities before removing targets, while shell resolution is stable.
        var ownedLinks = new List<FileRecord>();
        foreach (FileRecord link in state.Shortcuts)
            if (OwnedShortcut(link)) ownedLinks.Add(new FileRecord { Path = link.Path, Hash = Hash(link.Path) });
        foreach (KeyValuePair<string, string> item in targets) DeleteUnchanged(Child(root, item.Key), item.Value);
        foreach (FileRecord link in ownedLinks) DeleteUnchanged(link.Path, link.Hash);
        RemoveEmptyDirectory(Group);
        File.Delete(Child(root, StateName));
        using (RegistryKey user = UserRegistry()) user.DeleteSubKeyTree(RegistryPath, false);
        RemoveEmptyTree(root);
        progress("卸载完成。用户输出、额外文件和修改过的快捷方式已保留。");
    }
}
internal sealed class SetupForm : Form
{
    private readonly Options options;
    private TextBox directory;
    private CheckBox desktop;
    private Label status;
    private Button action, browse;
    private bool busy, completed;

    internal SetupForm(Options o)
    {
        options = o;
        Text = "FreeFrameGen" + (o.Uninstall ? " — 卸载 / Uninstall" : " — 安装 / Install");
        Font = new Font("Microsoft YaHei UI", 8F);
        ClientSize = new Size(760, 650);
        FormBorderStyle = FormBorderStyle.FixedDialog;
        MaximizeBox = false;
        MinimizeBox = false;
        StartPosition = FormStartPosition.CenterScreen;
        // The preview and the runtime form use the same fixed, roomy layout.
        // Disabling WinForms' implicit DPI rescale prevents labels from growing
        // beyond their bounded card on high-DPI machines.
        AutoScaleMode = AutoScaleMode.None;
        BackColor = Color.FromArgb(245, 248, 252);
        Icon appIcon = SetupProgram.ProductIcon();
        if (appIcon != null) Icon = appIcon;

        Panel header = new Panel { Location = new Point(0, 0), Size = new Size(760, 112), BackColor = Color.FromArgb(8, 18, 38) };
        Controls.Add(header);
        if (appIcon != null)
        {
            PictureBox logo = new PictureBox { Location = new Point(28, 22), Size = new Size(68, 68), SizeMode = PictureBoxSizeMode.StretchImage, Image = appIcon.ToBitmap() };
            header.Controls.Add(logo);
        }
        Label title = new Label { Text = "FreeFrameGen", Location = new Point(112, 23), Size = new Size(610, 36), Font = new Font(Font.FontFamily, 16F, FontStyle.Bold), ForeColor = Color.White };
        header.Controls.Add(title);
        header.Controls.Add(new Label { Text = "低延迟帧生成预览 · Low-latency frame generation preview", Location = new Point(114, 66), Size = new Size(610, 25), Font = new Font(Font.FontFamily, 7F), ForeColor = Color.FromArgb(178, 213, 235) });

        Panel card = new Panel { Location = new Point(24, 130), Size = new Size(712, 450), BackColor = Color.White, BorderStyle = BorderStyle.FixedSingle };
        Controls.Add(card);
        Label intro = new Label {
            Text = o.Uninstall
                ? "卸载前会检查程序文件和安装记录。\r\nUninstall checks the program files and installation record first.\r\n已修改的文件、额外文件和用户输出会保留。 / Modified files and user data are kept."
                : "安装显示器/窗口捕获预览、Native SDK 和诊断工具。\r\nInstall the capture preview, Native SDK and diagnostics in a separate folder.\r\n不会写入游戏目录、注入 DLL 或开启自启动。 / No game files, DLL injection or auto-start.",
            Location = new Point(24, 20), Size = new Size(660, 98), Font = new Font(Font.FontFamily, 8F), ForeColor = Color.FromArgb(36, 49, 66) };
        card.Controls.Add(intro);

        card.Controls.Add(new Label { Text = "安装位置 / Install location", Location = new Point(24, 128), Size = new Size(650, 22), Font = new Font(Font.FontFamily, 8F, FontStyle.Bold), ForeColor = Color.FromArgb(36, 49, 66) });
        directory = new TextBox { Location = new Point(24, 156), Size = new Size(570, 30), Text = o.Destination ?? SetupProgram.DefaultDir, ReadOnly = o.Uninstall, BackColor = Color.White, Font = new Font(Font.FontFamily, 6.5F) };
        card.Controls.Add(directory);
        browse = new Button { Text = "浏览 / Browse", Location = new Point(606, 154), Size = new Size(78, 32), Enabled = !o.Uninstall, FlatStyle = FlatStyle.System, Font = new Font(Font.FontFamily, 7F) };
        browse.Click += delegate
        {
            using (FolderBrowserDialog dialog = new FolderBrowserDialog { Description = "选择父目录 / Choose a parent folder; " + Product.Folder + " will be added", ShowNewFolderButton = true })
                if (dialog.ShowDialog(this) == DialogResult.OK) directory.Text = Path.Combine(dialog.SelectedPath, Product.Folder);
        };
        card.Controls.Add(browse);
        card.Controls.Add(new Label { Text = o.Uninstall
            ? "当前安装记录指向的目录。不会删除用户输出。\r\nThe recorded folder is used; user output is preserved."
            : "请选择一个新的独立目录，最后一级必须是 FreeFrameGen。\r\nChoose a new standalone folder ending in FreeFrameGen.",
            Location = new Point(24, 195), Size = new Size(660, 52), Font = new Font(Font.FontFamily, 7F), ForeColor = Color.FromArgb(104, 119, 136) });

        // Interactive installation defaults to a desktop shortcut. Silent deployments remain
        // opt-in through --desktop so automation does not unexpectedly touch the desktop.
        desktop = new CheckBox {
            Text = "创建桌面快捷方式 / Create desktop shortcut",
            Location = new Point(24, 258), Size = new Size(660, 28), Font = new Font(Font.FontFamily, 8F), Checked = o.Desktop || !o.Silent,
            Visible = !o.Uninstall, AutoSize = false
        };
        card.Controls.Add(desktop);
        status = new Label {
            Text = o.Uninstall ? "准备卸载。 / Ready to uninstall." : "安装程序未签名。请仅使用可信来源的构建。\r\nThis installer is unsigned; use a trusted build.",
            Location = new Point(24, 302), Size = new Size(660, 100), Font = new Font(Font.FontFamily, 7F), ForeColor = Color.FromArgb(104, 119, 136)
        };
        card.Controls.Add(status);

        Label footer = new Label { Text = "FreeFrameGen Beta · Windows 10/11 x64 · " + Product.Version, Location = new Point(26, 600), Size = new Size(430, 28), Font = new Font(Font.FontFamily, 7F), ForeColor = Color.FromArgb(104, 119, 136) };
        Controls.Add(footer);
        Button cancel = new Button { Text = "关闭 / Close", Location = new Point(548, 594), Size = new Size(88, 36), FlatStyle = FlatStyle.System, Font = new Font(Font.FontFamily, 8F) };
        cancel.Click += delegate { if (!busy) Close(); };
        Controls.Add(cancel);
        action = new Button { Text = o.Uninstall ? "卸载 / Uninstall" : "安装 / Install", Location = new Point(644, 594), Size = new Size(92, 36), Font = new Font(Font.FontFamily, 8F), BackColor = Color.FromArgb(28, 100, 210), ForeColor = Color.White, FlatStyle = FlatStyle.Flat };
        action.FlatAppearance.BorderSize = 0;
        action.Click += delegate
        {
            if (completed) { Close(); return; }
            busy = true; action.Enabled = false; cancel.Enabled = false; browse.Enabled = false; directory.Enabled = false; desktop.Enabled = false;
            options.Destination = directory.Text;
            options.Desktop = desktop.Checked;
            try
            {
                SetupProgram.Execute(options, delegate(string message) { status.Text = message; status.Refresh(); SetupProgram.Log(options, message); });
                SetupProgram.Log(options, "RESULT=SUCCESS");
                status.Text = options.Uninstall ? "卸载完成。/ Uninstall complete." : "安装完成。桌面和开始菜单入口已创建。\r\nInstallation complete. Desktop and Start menu shortcuts are ready.";
                action.Text = "完成 / Done";
                completed = true;
            }
            catch (Exception e)
            {
                SetupProgram.Log(options, "RESULT=FAILED " + e.Message);
                status.Text = "操作未完成。/ The operation did not complete.";
                MessageBox.Show(this, e.Message, Text, MessageBoxButtons.OK, MessageBoxIcon.Warning);
                action.Enabled = true;
                if (!options.Uninstall) { directory.Enabled = true; browse.Enabled = true; desktop.Enabled = true; }
            }
            finally { busy = false; cancel.Enabled = true; }
        };
        Controls.Add(action);
        FormClosing += delegate(object sender, FormClosingEventArgs e) { if (busy) e.Cancel = true; };
    }
}
