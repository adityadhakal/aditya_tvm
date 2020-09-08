cat resnet18_and_mobilenet_single_server.txt | sed -n '/\[/p' | awk '{print $1}' | cut -d "[" -f2 | cut -d "]" -f1
