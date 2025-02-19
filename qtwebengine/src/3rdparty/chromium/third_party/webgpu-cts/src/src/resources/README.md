Always use `getResourcePath()` to get the appropriate path to these resources depending
on the context (WPT, standalone, worker, etc.)


The test video files were generated with by ffmpeg cmds below:
```
// Generate four-colors-vp8-bt601.webm, mimeType: 'video/webm; codecs=vp8'
ffmpeg.exe -loop 1 -i .\four-colors.png -c:v libvpx -pix_fmt yuv420p -frames 50 -colorspace smpte170m -color_primaries smpte170m -color_trc smpte170m -color_range tv four-colors-vp8-bt601.webm

// Generate four-colors-theora-bt601.ogv, mimeType: 'video/ogg; codecs=theora'
ffmpeg.exe -loop 1 -i .\four-colors.png -c:v libtheora -pix_fmt yuv420p -frames 50 -colorspace smpte170m -color_primaries smpte170m -color_trc smpte170m -color_range tv four-colors-theora-bt601.ogv

// Generate four-colors-h264-bt601.mp4, mimeType: 'video/mp4; codecs=h264'
ffmpeg.exe -loop 1 -i .\four-colors.png -c:v libx264 -pix_fmt yuv420p -frames 50 -colorspace smpte170m -color_primaries smpte170m -color_trc smpte170m -color_range tv four-colors-h264-bt601.mp4

// Generate four-colors-vp9-bt601.webm, mimeType: 'video/webm; codecs=vp9'
ffmpeg.exe -loop 1 -i .\four-colors.png -c:v libvpx-vp9 -pix_fmt yuv420p -frames 50 -colorspace smpte170m -color_primaries smpte170m -color_trc smpte170m -color_range tv four-colors-vp9-bt601.webm

// Generate four-colors-vp9-bt709.webm, mimeType: 'video/webm; codecs=vp9'
ffmpeg.exe -loop 1 -i .\four-colors.png -c:v libvpx-vp9 -pix_fmt yuv420p -frames 50 -colorspace bt709 -color_primaries bt709 -color_trc bt709 -color_range tv -vf scale=out_color_matrix=bt709:out_range=tv four-colors-vp9-bt709.webm

// Generate  four-colors-vp9-bt601.mp4, mimeType: 'video/mp4; codecs=vp9'
ffmpeg.exe -loop 1 -i .\four-colors.png -c:v libvpx-vp9 -pix_fmt yuv420p -frames 50 -colorspace smpte170m -color_primaries smpte170m -color_trc smpte170m -color_range tv four-colors-vp9-bt601.mp4
```

Generate video files to test rotation behaviour.
Use ffmepg to rotate video content x degrees in cw direction (by using `transpose`) and update transform matrix in metadata through `display_rotation` to x degrees to apply ccw direction rotation.

H264 rotated video files are generated by ffmpeg cmds below:
```
// Generate four-colors-h264-bt601-rotate-90.mp4, mimeType: 'video/mp4; codecs=h264'
ffmpeg.exe -loop 1 -i .\four-colors.png -c:v libx264 -pix_fmt yuv420p -frames 50 -colorspace smpte170m -color_primaries smpte170m -color_trc smpte170m -color_range tv -vf transpose=2 temp.mp4
ffmpeg -display_rotation 270 -i temp.mp4 -c copy four-colors-h264-bt601-rotate-90.mp4
rm temp.mp4

// Generate four-colors-h264-bt601-rotate-180.mp4, mimeType: 'video/mp4; codecs=h264'
ffmpeg.exe -loop 1 -i .\four-colors.png -c:v libx264 -pix_fmt yuv420p -frames 50 -colorspace smpte170m -color_primaries smpte170m -color_trc smpte170m -color_range tv -vf transpose=2,transpose=2 temp.mp4
ffmpeg -display_rotation 180 -i temp.mp4 -c copy four-colors-h264-bt601-rotate-180.mp4
rm temp.mp4

// Generate four-colors-h264-bt601-rotate-270.mp4, mimeType: 'video/mp4; codecs=h264'
ffmpeg.exe -loop 1 -i .\four-colors.png -c:v libx264 -pix_fmt yuv420p -frames 50 -colorspace smpte170m -color_primaries smpte170m -color_trc smpte170m -color_range tv -vf transpose=1 temp.mp4
ffmpeg -display_rotation 90 -i temp.mp4 -c copy four-colors-h264-bt601-rotate-270.mp4
rm temp.mp4

```

Vp9 rotated video files are generated by ffmpeg cmds below:
```
// Generate four-colors-h264-bt601-rotate-90.mp4, mimeType: 'video/mp4; codecs=vp9'
ffmpeg.exe -loop 1 -i .\four-colors.png -c:v libvpx-vp9 -pix_fmt yuv420p -frames 50 -colorspace smpte170m -color_primaries smpte170m -color_trc smpte170m -color_range tv -vf transpose=2 temp.mp4
ffmpeg -display_rotation 270 -i temp.mp4 -c copy four-colors-vp9-bt601-rotate-90.mp4
rm temp.mp4

// Generate four-colors-h264-bt601-rotate-180.mp4, mimeType: 'video/mp4; codecs=vp9'
ffmpeg.exe -loop 1 -i .\four-colors.png -c:v libvpx-vp9 -pix_fmt yuv420p -frames 50 -colorspace smpte170m -color_primaries smpte170m -color_trc smpte170m -color_range tv -vf transpose=2,transpose=2 temp.mp4
ffmpeg -display_rotation 180 -i temp.mp4 -c copy four-colors-vp9-bt601-rotate-180.mp4
rm temp.mp4

// Generate four-colors-h264-bt601-rotate-270.mp4, mimeType: 'video/mp4; codecs=vp9'
ffmpeg.exe -loop 1 -i .\four-colors.png -c:v libvpx-vp9 -pix_fmt yuv420p -frames 50 -colorspace smpte170m -color_primaries smpte170m -color_trc smpte170m -color_range tv -vf transpose=1 temp.mp4
ffmpeg -display_rotation 90 -i temp.mp4 -c copy four-colors-vp9-bt601-rotate-270.mp4
rm temp.mp4

```