using System.Net;
using System.Text;
using VoiceInput.Robot;
internal static class DiscoveryContract
{
    public static int RunAll()
    {
        bool Parse(string value,out PairableRobot? robot)=>WifiRobotPairing.TryParseDiscovery(Encoding.UTF8.GetBytes(value),IPAddress.Loopback,out robot);
        const string payload="{\"v\":1,\"name\":\"DIY Voice Robot\",\"device\":\"001122aabbcc\",\"port\":45834,\"security\":2,\"patch\":1,\"remaining\":120}";
        if(!Parse(payload,out var robot)||robot?.Name!="DIY Voice Robot")throw new Exception("Custom robot name was not preserved");
        foreach(var invalid in new[]{payload.Replace("\"security\":2","\"security\":1"),payload.Replace("DIY Voice Robot",new string('x',65)),payload.Replace("DIY Voice Robot","bad\\nname"),"[]","{"})
            if(Parse(invalid,out _))throw new Exception("Invalid discovery accepted");
        Console.WriteLine("PASS custom robot discovery name with bounded metadata and no security downgrade");
        return 1;
    }
}
