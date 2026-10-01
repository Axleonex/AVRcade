// Controlled managed fixture for the M6 managed-locator CTest (AAA-01/AAA-03).
// This is a TEST FIXTURE ONLY — never a real game. The read-only Mono locator
// enumerates this assembly's metadata and must find `FixtureType.FixtureMethod`
// as a `mono_method` candidate.
//
// Built to a tiny managed assembly by the native test harness (Task 2.1); its
// exact class/method names are the assertion targets.

namespace VrClient.ManagedLocatorFixture
{
    public class FixtureType
    {
        // The known method the locator must enumerate.
        public int FixtureMethod(int value)
        {
            return value + 1;
        }

        // A second method so the enumeration returns more than one candidate.
        public void SecondMethod()
        {
        }
    }
}
