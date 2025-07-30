#!/usr/bin/env python3
"""
Test script to verify Jumperless sample rate selector functionality.
This script tests the libsigrok driver's sample rate configuration.
"""

import subprocess
import sys
import time

def test_driver_capabilities():
    """Test that the driver properly advertises sample rate capabilities."""
    print("Testing Jumperless driver sample rate capabilities...")
    
    try:
        # Use sigrok-cli to query driver capabilities
        result = subprocess.run([
            'sigrok-cli', 
            '--driver', 'jumperless-mixed-signal',
            '--show'
        ], capture_output=True, text=True, timeout=10)
        
        if result.returncode == 0:
            print("✓ Driver loaded successfully")
            print("Driver capabilities:")
            print(result.stdout)
            
            # Check if sample rates are listed
            if 'samplerate' in result.stdout.lower():
                print("✓ Sample rate configuration is advertised")
                return True
            else:
                print("✗ Sample rate configuration not found in capabilities")
                return False
        else:
            print("✗ Driver failed to load:")
            print(result.stderr)
            return False
            
    except subprocess.TimeoutExpired:
        print("✗ Driver query timeout")
        return False
    except FileNotFoundError:
        print("✗ sigrok-cli not found. Please install sigrok-cli to test.")
        return False

def test_sample_rate_list():
    """Test that the driver lists available sample rates."""
    print("\nTesting sample rate list...")
    
    try:
        # Try to scan for devices and get sample rate list
        result = subprocess.run([
            'sigrok-cli',
            '--driver', 'jumperless-mixed-signal',
            '--config', 'samplerate'
        ], capture_output=True, text=True, timeout=10)
        
        if result.returncode == 0:
            print("✓ Sample rate query successful")
            print("Available sample rates:")
            print(result.stdout)
            
            # Check for expected rates
            expected_rates = ['1000', '2000', '5000', '10000', '100000', '1000000']
            found_rates = []
            
            for rate in expected_rates:
                if rate in result.stdout:
                    found_rates.append(rate)
            
            if found_rates:
                print(f"✓ Found expected rates: {found_rates}")
                return True
            else:
                print("✗ No expected sample rates found")
                return False
        else:
            print("✗ Sample rate query failed:")
            print(result.stderr)
            return False
            
    except subprocess.TimeoutExpired:
        print("✗ Sample rate query timeout")
        return False

def test_sample_rate_setting():
    """Test setting a specific sample rate."""
    print("\nTesting sample rate setting...")
    
    try:
        # Try to set a specific sample rate
        result = subprocess.run([
            'sigrok-cli',
            '--driver', 'jumperless-mixed-signal',
            '--config', 'samplerate=100000',
            '--show'
        ], capture_output=True, text=True, timeout=10)
        
        if result.returncode == 0:
            print("✓ Sample rate setting successful")
            if '100000' in result.stdout or '100 kHz' in result.stdout:
                print("✓ Sample rate confirmed as set")
                return True
            else:
                print("? Sample rate set but not confirmed in output")
                return True
        else:
            print("✗ Sample rate setting failed:")
            print(result.stderr)
            return False
            
    except subprocess.TimeoutExpired:
        print("✗ Sample rate setting timeout")
        return False

def main():
    """Run all tests."""
    print("Jumperless Sample Rate Selector Test")
    print("=" * 40)
    
    tests = [
        test_driver_capabilities,
        test_sample_rate_list,
        test_sample_rate_setting
    ]
    
    passed = 0
    total = len(tests)
    
    for test_func in tests:
        if test_func():
            passed += 1
        time.sleep(1)  # Brief pause between tests
    
    print("\n" + "=" * 40)
    print(f"Test Results: {passed}/{total} tests passed")
    
    if passed == total:
        print("✓ All tests passed! Sample rate selector should work in PulseView.")
    else:
        print("✗ Some tests failed. Check the libsigrok driver configuration.")
    
    return passed == total

if __name__ == "__main__":
    success = main()
    sys.exit(0 if success else 1) 