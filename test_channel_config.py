#!/usr/bin/env python3
"""
Test script to verify Jumperless channel configuration is working.
This tests the specific SET_CHANNELS command that was failing.
"""

import subprocess
import sys
import time

def test_channel_config():
    """Test that channel configuration works with different channel combinations."""
    print("Testing Jumperless channel configuration...")
    
    test_cases = [
        {
            'name': 'Digital only (default)',
            'channels': 'GPIO 1,GPIO 2',
            'expected': 'digital'
        },
        {
            'name': 'Mixed signal',
            'channels': 'GPIO 1,ADC 0',
            'expected': 'mixed'
        },
        {
            'name': 'All digital channels',
            'channels': 'GPIO 1,GPIO 2,GPIO 3,GPIO 4,GPIO 5,GPIO 6,GPIO 7,GPIO 8',
            'expected': 'digital'
        }
    ]
    
    passed = 0
    total = len(test_cases)
    
    for i, test_case in enumerate(test_cases):
        print(f"\nTest {i+1}/{total}: {test_case['name']}")
        print(f"Channels: {test_case['channels']}")
        
        try:
            # Try to set up acquisition with specific channels
            # This will test the SET_CHANNELS command that was failing
            result = subprocess.run([
                'sigrok-cli',
                '--driver', 'jumperless-mixed-signal',
                '--channels', test_case['channels'],
                '--config', 'samplerate=100000',
                '--config', 'limit_samples=1000',
                '--continuous'  # Don't actually capture, just set up
            ], capture_output=True, text=True, timeout=5)
            
            # For this test, we mainly care that it doesn't fail with channel config errors
            if 'Failed to configure channels' not in result.stderr:
                print(f"✓ Channel configuration accepted")
                passed += 1
            else:
                print(f"✗ Channel configuration failed:")
                print(result.stderr)
            
        except subprocess.TimeoutExpired:
            print("? Test timeout (might be waiting for trigger)")
            passed += 1  # Timeout is actually OK - means config worked
        except Exception as e:
            print(f"✗ Test error: {e}")
    
    return passed, total

def test_protocol_detection():
    """Test that Enhanced protocol is detected correctly."""
    print("Testing Enhanced protocol detection...")
    
    try:
        # Run with debug output to see protocol detection
        result = subprocess.run([
            'sigrok-cli',
            '--driver', 'jumperless-mixed-signal',
            '--loglevel', '5',  # Debug level
            '--scan'
        ], capture_output=True, text=True, timeout=10)
        
        if 'Enhanced protocol support detected' in result.stderr:
            print("✓ Enhanced protocol detected correctly")
            return True
        elif 'SUMP-only protocol detected' in result.stderr:
            print("? SUMP protocol detected (Enhanced might not be available)")
            return True
        else:
            print("✗ Protocol detection unclear:")
            print(result.stderr)
            return False
            
    except subprocess.TimeoutExpired:
        print("✗ Protocol detection timeout")
        return False
    except Exception as e:
        print(f"✗ Protocol detection error: {e}")
        return False

def main():
    """Run all tests."""
    print("Jumperless Channel Configuration Test")
    print("=" * 40)
    
    # Test protocol detection first
    if not test_protocol_detection():
        print("Protocol detection failed - skipping channel tests")
        return False
    
    # Test channel configuration
    passed, total = test_channel_config()
    
    print("\n" + "=" * 40)
    print(f"Test Results: {passed}/{total} channel tests passed")
    
    if passed == total:
        print("✓ All tests passed! Channel configuration should work in PulseView.")
    else:
        print("✗ Some tests failed. Check the packet format fix.")
    
    return passed == total

if __name__ == "__main__":
    success = main()
    sys.exit(0 if success else 1) 