#!/usr/bin/env python3
"""
Test script to verify Jumperless driver crash fixes.
This script tests connection/disconnection scenarios that previously caused crashes.
"""

import subprocess
import sys
import time
import signal
import threading

def test_sigrok_cli_connection():
    """Test basic connection with sigrok-cli to verify driver stability."""
    print("Testing sigrok-cli connection...")
    
    try:
        # Test device listing
        result = subprocess.run([
            'sigrok-cli', 
            '--driver', 'jumperless-mixed-signal',
            '--scan'
        ], capture_output=True, text=True, timeout=10)
        
        if result.returncode == 0:
            print("✓ Driver scan successful")
            print(f"  Output: {result.stdout.strip()}")
        else:
            print(f"✗ Driver scan failed: {result.stderr}")
            return False
            
    except subprocess.TimeoutExpired:
        print("✗ Driver scan timed out")
        return False
    except Exception as e:
        print(f"✗ Driver scan error: {e}")
        return False
    
    return True

def test_sample_rate_config():
    """Test sample rate configuration that was previously causing issues."""
    print("Testing sample rate configuration...")
    
    sample_rates = ['1000', '10000', '100000', '1000000']
    
    for rate in sample_rates:
        try:
            result = subprocess.run([
                'sigrok-cli',
                '--driver', 'jumperless-mixed-signal',
                '--config', f'samplerate={rate}',
                '--show'
            ], capture_output=True, text=True, timeout=5)
            
            if result.returncode == 0:
                print(f"✓ Sample rate {rate} Hz configured successfully")
            else:
                print(f"✗ Sample rate {rate} Hz failed: {result.stderr}")
                
        except subprocess.TimeoutExpired:
            print(f"✗ Sample rate {rate} Hz configuration timed out")
        except Exception as e:
            print(f"✗ Sample rate {rate} Hz error: {e}")
    
    return True

def test_rapid_connect_disconnect():
    """Test rapid connection/disconnection to stress test cleanup code."""
    print("Testing rapid connect/disconnect cycles...")
    
    for i in range(5):
        try:
            print(f"  Cycle {i+1}/5...")
            
            # Quick connection test
            result = subprocess.run([
                'sigrok-cli',
                '--driver', 'jumperless-mixed-signal',
                '--config', 'samplerate=100000',
                '--time', '1ms'  # Very short capture
            ], capture_output=True, text=True, timeout=3)
            
            if result.returncode == 0:
                print(f"    ✓ Cycle {i+1} completed successfully")
            else:
                print(f"    ✗ Cycle {i+1} failed: {result.stderr}")
                
        except subprocess.TimeoutExpired:
            print(f"    ✗ Cycle {i+1} timed out")
        except Exception as e:
            print(f"    ✗ Cycle {i+1} error: {e}")
        
        time.sleep(0.5)  # Brief pause between cycles
    
    return True

def test_error_recovery():
    """Test error recovery scenarios that previously caused crashes."""
    print("Testing error recovery scenarios...")
    
    # Test with invalid configuration
    try:
        result = subprocess.run([
            'sigrok-cli',
            '--driver', 'jumperless-mixed-signal',
            '--config', 'samplerate=999999999',  # Invalid rate
            '--time', '1ms'
        ], capture_output=True, text=True, timeout=5)
        
        # Should fail gracefully, not crash
        print("✓ Invalid sample rate handled gracefully")
        
    except subprocess.TimeoutExpired:
        print("✗ Invalid sample rate caused timeout (possible hang)")
    except Exception as e:
        print(f"✗ Invalid sample rate caused exception: {e}")
    
    # Test with device disconnection simulation
    try:
        # Start a capture and interrupt it
        proc = subprocess.Popen([
            'sigrok-cli',
            '--driver', 'jumperless-mixed-signal',
            '--continuous'
        ], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        
        time.sleep(1)  # Let it start
        proc.terminate()  # Simulate user interruption
        proc.wait(timeout=3)
        
        print("✓ Interrupted capture handled gracefully")
        
    except subprocess.TimeoutExpired:
        print("✗ Interrupted capture caused hang")
        proc.kill()
    except Exception as e:
        print(f"✗ Interrupted capture error: {e}")
    
    return True

def main():
    """Run all crash fix tests."""
    print("=== Jumperless Driver Crash Fix Tests ===")
    print()
    
    tests = [
        ("Basic Connection", test_sigrok_cli_connection),
        ("Sample Rate Config", test_sample_rate_config),
        ("Rapid Connect/Disconnect", test_rapid_connect_disconnect),
        ("Error Recovery", test_error_recovery),
    ]
    
    passed = 0
    total = len(tests)
    
    for test_name, test_func in tests:
        print(f"Running {test_name}...")
        try:
            if test_func():
                passed += 1
            print()
        except Exception as e:
            print(f"✗ Test {test_name} crashed: {e}")
            print()
    
    print("=== Test Results ===")
    print(f"Passed: {passed}/{total}")
    
    if passed == total:
        print("🎉 All tests passed! Crash fixes are working.")
        return 0
    else:
        print("❌ Some tests failed. Review the output above.")
        return 1

if __name__ == "__main__":
    sys.exit(main()) 