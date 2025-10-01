#!/bin/bash

#DO NOT REMOVE THE FOLLOWING LINES
git add $0 >> .local.git.out
git commit -a -m "Lab 2 commit" >> .local.git.out
git push >> .local.git.out || echo


#Your code here
#!/bin/bash

# Function to print help message
print_help() {
  echo "./pwcheck.sh [-f passwordFile] [password1 password2 password3…]"
}

# Function to calculate password score
calculate_score() {
  password=$1

  # Check password length
  length=${#password}
  if [ $length -lt 6 ] || [ $length -gt 32 ]; then
    echo "Error: Password length invalid."
    return
  fi

  # Check for special characters
  if ! [[ $password =~ [#\$+%@^*\-/] ]]; then
    echo "Error: Password should include at least one of \"#\$+%@^*-/\""
    return
  fi

  # Check for numbers
  if ! [[ $password =~ [0-9] ]]; then
    echo "Error: Password should include at least one number \"0-9\""
    return
  fi

  # Check for uppercase and lowercase letters
  if ! [[ $password =~ [A-Z] ]] || ! [[ $password =~ [a-z] ]]; then
    echo "Error: Passwords should have at least one Uppercase and lowercase alphabetic character."
    return
  fi

  # Initialize score
  score=0

  # Add points: +1 for each character
  score=$((score + length))

  # Add points: +2 for each special character
  special_chars=$(echo "$password" | egrep -o "[#\$+%@^*/\-]" | wc -l)
  score=$((score + special_chars * 2))

  # Add points: +2 for each number
  numbers=$(echo "$password" | egrep -o "[0-9]" | wc -l)
  score=$((score + numbers * 2))

  # Add points: +1 for each alphabetic character
  alpha_chars=$(echo "$password" | egrep -o "[A-Za-z]" | wc -l)
  score=$((score + alpha_chars))

  # Deductions
  # Corrected repeated alphanumeric character check:
  # Deduct 10 points only if there are two or more **consecutive identical alphanumeric characters**.
  if echo "$password" | egrep -Eq '([[:alnum:]])\1'; then
    score=$((score - 10))
  fi

  # Deduct 3 points for 3+ consecutive lowercase letters
  if [[ $password =~ [a-z]{3,} ]]; then
    score=$((score - 3))
  fi

  # Deduct 3 points for 3+ consecutive uppercase letters
  if [[ $password =~ [A-Z]{3,} ]]; then
    score=$((score - 3))
  fi

  # Deduct 3 points for 3+ consecutive numbers
  if [[ $password =~ [0-9]{3,} ]]; then
    score=$((score - 3))
  fi

  echo "$score"
}

# Main script logic
if [ $# -eq 0 ]; then
  print_help
  exit 0
fi

passwords=()

# Process arguments
while [[ $# -gt 0 ]]; do
  case "$1" in
    -f)
      shift
      if [ -f "$1" ]; then
        while IFS= read -r line; do
          passwords+=("$line")
        done < "$1"
      else
        echo "Error: File not found."
        exit 1
      fi
      ;;
    *)
      passwords+=("$1")
      ;;
  esac
  shift
done

# Print results
if [ ${#passwords[@]} -gt 0 ]; then
  printf "%-25s %s\n" "Password" "Score"
  printf "%-25s %s\n" "-------------------------" "-----"
  for password in "${passwords[@]}"; do
    printf "%-25s %s\n" "$password" "$(calculate_score "$password")"
  done
fi
