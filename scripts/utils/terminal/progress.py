#!/usr/bin/env python3
"""
Progress indicators for long-running operations.
"""

import sys
import time
import threading
from typing import Optional


class ProgressIndicator:
    """A simple progress indicator with spinner and elapsed time."""
    
    # Unicode braille spinner frames for smooth animation
    SPINNER_FRAMES = ["⠋", "⠙", "⠹", "⠸", "⠼", "⠴", "⠦", "⠧", "⠇", "⠏"]
    
    # Alternative ASCII spinner for terminals that don't support Unicode
    ASCII_SPINNER_FRAMES = ["|", "/", "-", "\\"]
    
    def __init__(self, message: str = "Processing", use_unicode: bool = True):
        """Initialize progress indicator.
        
        Args:
            message: The message to display with the spinner
            use_unicode: Whether to use Unicode spinner (vs ASCII)
        """
        self.message = message
        self.frames = self.SPINNER_FRAMES if use_unicode else self.ASCII_SPINNER_FRAMES
        self.running = False
        self.thread: Optional[threading.Thread] = None
        self.start_time = 0.0
        self.frame_index = 0
        
    def _animate(self):
        """Animation loop that runs in a separate thread."""
        while self.running:
            elapsed = time.time() - self.start_time
            frame = self.frames[self.frame_index % len(self.frames)]
            
            # Clear the line and print the progress message
            sys.stdout.write(f"\r{self.message}... {frame} ({elapsed:.1f}s)")
            sys.stdout.flush()
            
            self.frame_index += 1
            time.sleep(0.1)  # Update 10 times per second
            
    def start(self):
        """Start the progress indicator."""
        if not self.running:
            self.running = True
            self.start_time = time.time()
            self.frame_index = 0
            self.thread = threading.Thread(target=self._animate, daemon=True)
            self.thread.start()
            
    def stop(self, success: bool = True, custom_message: Optional[str] = None):
        """Stop the progress indicator and clear the line.
        
        Args:
            success: Whether the operation was successful
            custom_message: Optional custom completion message
        """
        if self.running:
            self.running = False
            if self.thread:
                self.thread.join(timeout=0.5)  # Wait max 0.5s for thread to finish
                
            # Clear the progress line
            sys.stdout.write("\r" + " " * 80 + "\r")  # Clear with spaces
            sys.stdout.flush()
            
            # Optionally print a completion message
            if custom_message:
                print(custom_message)
                
    def __enter__(self):
        """Context manager entry."""
        self.start()
        return self
        
    def __exit__(self, exc_type, exc_val, exc_tb):
        """Context manager exit."""
        # Stop with success=False if there was an exception
        self.stop(success=(exc_type is None))
        

class DummyProgressIndicator:
    """A no-op progress indicator for when progress display is not wanted."""
    
    def __init__(self, *args, **kwargs):
        pass
        
    def start(self):
        pass
        
    def stop(self, *args, **kwargs):
        pass
        
    def __enter__(self):
        return self
        
    def __exit__(self, *args):
        pass


def create_progress(message: str = "Processing", enabled: bool = True) -> ProgressIndicator:
    """Factory function to create appropriate progress indicator.
    
    Args:
        message: The message to display
        enabled: Whether to actually show progress (returns dummy if False)
        
    Returns:
        Either a real ProgressIndicator or a DummyProgressIndicator
    """
    if enabled and sys.stdout.isatty():
        # Only show progress if we're in an interactive terminal
        try:
            # Try to use Unicode spinner
            return ProgressIndicator(message, use_unicode=True)
        except:
            # Fallback to ASCII if Unicode fails
            return ProgressIndicator(message, use_unicode=False)
    else:
        return DummyProgressIndicator()