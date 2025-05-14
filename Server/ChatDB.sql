-- 데이터베이스 생성
IF NOT EXISTS (SELECT name FROM sys.databases WHERE name = N'ChatDB')
BEGIN
    CREATE DATABASE ChatDB;
END
GO

USE ChatDB;
GO

-- Users 테이블 생성
IF NOT EXISTS (SELECT * FROM sysobjects WHERE name='Users' AND xtype='U')
BEGIN
    CREATE TABLE Users (
        UserID INT IDENTITY(1,1) PRIMARY KEY,
        Username NVARCHAR(50) NOT NULL UNIQUE,
        Password NVARCHAR(100) NOT NULL,
        CreatedAt DATETIME DEFAULT GETDATE()
    );
END
GO

-- ChatLogs 테이블 생성
IF NOT EXISTS (SELECT * FROM sysobjects WHERE name='ChatLogs' AND xtype='U')
BEGIN
    CREATE TABLE ChatLogs (
        LogID INT IDENTITY(1,1) PRIMARY KEY,
        UserID INT NOT NULL,
        Message NVARCHAR(MAX) NOT NULL,
        SentAt DATETIME DEFAULT GETDATE(),
        FOREIGN KEY (UserID) REFERENCES Users(UserID)
    );
END
GO 